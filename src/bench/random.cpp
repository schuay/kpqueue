/*
 *  Copyright 2014 Jakob Gruber
 *
 *  This file is part of kpqueue.
 *
 *  kpqueue is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  kpqueue is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with kpqueue.  If not, see <http://www.gnu.org/licenses/>.
 */

// #define ENABLE_QUALITY 1

#include <ctime>
#include <future>
#include <getopt.h>
#include <queue>
#include <random>
#include <thread>
#include <unistd.h>

#ifdef HAVE_VALGRIND
#include <valgrind/callgrind.h>
#endif

#include "pqs/cheap.h"
#include "pqs/globallock.h"
#include "pqs/multiq.h"
#include "pqs/sequence_heap.h"
#include "pqs/skip_queue.h"
#include "dist_lsm/dist_lsm.h"
#include "k_lsm/k_lsm.h"
#include "multi_lsm/multi_lsm.h"
#include "sequential_lsm/lsm.h"
#include "shared_lsm/shared_lsm.h"
#include "util/counters.h"
#include "itree.h"
#include "util.h"

#define PQ_CHEAP      "cheap"
#define PQ_DLSM       "dlsm"
#define PQ_GLOBALLOCK "globallock"
#define PQ_KLSM16     "klsm16"
#define PQ_KLSM128    "klsm128"
#define PQ_KLSM256    "klsm256"
#define PQ_KLSM4096   "klsm4096"
#define PQ_LSM        "lsm"
#define PQ_MLSM       "mlsm"
#define PQ_MULTIQ     "multiq"
#define PQ_SEQUENCE   "sequence"
#define PQ_SKIP       "skip"
#define PQ_SLSM       "slsm"

#ifdef ENABLE_QUALITY
#define KEY_TYPE      uint32_t
#define VAL_TYPE      packed_item_id
#else
#define KEY_TYPE      uint32_t
#define VAL_TYPE      uint32_t
#endif

/**
 * Uniform: Each thread performs 50% inserts, 50% deletes.
 * Split: 50% of threads perform inserts, 50% of threads perform deletes (in case of an
 *        odd thread count there are more inserts than deletes).
 * Producer: A single thread performs inserts, all others delete.
 */
enum {
    WORKLOAD_UNIFORM = 0,
    WORKLOAD_SPLIT,
    WORKLOAD_PRODUCER,
    WORKLOAD_ALTERNATING,
    WORKLOAD_COUNT,
};

/**
 * Uniform: Keys are generated uniformly at random.
 * Ascending: Keys are generated uniformly at random within a smaller integer range [x, x + z]
 *            s.t. x rises over time.
 */
enum {
    KEYS_UNIFORM = 0,
    KEYS_ASCENDING,
    KEYS_DESCENDING,
    KEYS_RESTRICTED_8,
    KEYS_RESTRICTED_16,
    KEYS_COUNT,
};

constexpr int DEFAULT_SEED       = 0;
constexpr int DEFAULT_SIZE       = 1000000;  // Matches benchmarks from klsm paper.
constexpr int DEFAULT_NTHREADS   = 1;
constexpr int DEFAULT_RELAXATION = 256;
#ifdef ENABLE_QUALITY
constexpr int DEFAULT_SLEEP      = 1;
#else
constexpr int DEFAULT_SLEEP      = 10;
#endif
constexpr auto DEFAULT_COUNTERS  = false;
constexpr auto DEFAULT_WORKLOAD  = WORKLOAD_UNIFORM;
constexpr auto DEFAULT_KEYS      = KEYS_UNIFORM;

struct settings {
    int nthreads;
    int seed;
    int size;
    std::string type;
    bool print_counters;
    int keys;
    int workload;

    bool are_valid() const {
        if (nthreads < 1
                || size < 1
                || keys < 0 || keys >= KEYS_COUNT
                || workload < 0 || workload >= WORKLOAD_COUNT) {
            return false;
        }

        return true;
    }
};

static hwloc_wrapper hwloc;

static std::atomic<int> fill_barrier;
static std::atomic<bool> start_barrier(false);
static std::atomic<bool> end_barrier(false);

struct packed_item_id {
    uint32_t thread_id    : 9;
    uint32_t element_id   : 23;
    uint64_t tick;
};

class packed_uniform_bool_distribution {
public:
    packed_uniform_bool_distribution() :
        m_rand_int(0ULL, std::numeric_limits<uint64_t>::max()),
        m_iteration(0)
    {
    }

    bool operator()(std::mt19937 &gen) {
        if (m_iteration == 0) {
            m_packed = m_rand_int(gen);
        }

        const bool ret = (m_packed >> m_iteration) & 1;
        m_iteration = (m_iteration + 1) & MASK;
        return ret;
    }

private:
    std::uniform_int_distribution<uint64_t> m_rand_int;

    constexpr static int ITERATIONS = 64;
    constexpr static int MASK = ITERATIONS - 1;

    int m_iteration;
    int64_t m_packed;
};

static void
usage()
{
    fprintf(stderr,
            "USAGE: random [-c] [-i size] [-k keys] [-p nthreads] [-s seed] [-w workload] pq\n"
            "       -c: Print performance counters (default = %d)\n"
            "       -i: Specifies the initial size of the priority queue (default = %d)\n"
            "       -k: Specifies the key generation type, one of %d: uniform, %d: ascending, %d: descending,"
            "           %d: restricted (8-bit), %d: restricted (16-bit) (default = %d)\n"
            "       -p: Specifies the number of threads (default = %d)\n"
            "       -s: Specifies the value used to seed the random number generator (default = %d)\n"
            "       -w: Specifies the workload type, one of %d: uniform, %d: split, %d: producer, %d: alternating (default = %d)\n"
            "       pq: The data structure to use as the backing priority queue\n"
            "           (one of '%s', '%s', '%s', '%s', '%s', '%s',\n"
            "                   '%s', '%s', '%s', '%s', '%s', '%s',\n"
            "                   '%s')\n",
            DEFAULT_COUNTERS,
            DEFAULT_SIZE,
            KEYS_UNIFORM, KEYS_ASCENDING, KEYS_DESCENDING, KEYS_RESTRICTED_8, KEYS_RESTRICTED_16, DEFAULT_KEYS,
            DEFAULT_NTHREADS,
            DEFAULT_SEED,
            WORKLOAD_UNIFORM, WORKLOAD_SPLIT, WORKLOAD_PRODUCER, WORKLOAD_ALTERNATING, DEFAULT_WORKLOAD,
            PQ_CHEAP, PQ_DLSM, PQ_GLOBALLOCK, PQ_KLSM16, PQ_KLSM128, PQ_KLSM256, PQ_KLSM4096,
            PQ_LSM, PQ_MLSM, PQ_MULTIQ, PQ_SEQUENCE, PQ_SKIP, PQ_SLSM);
    exit(EXIT_FAILURE);
}

class workload_uniform {
public:
    workload_uniform(const struct settings &settings,
                     const int thread_id) :
        m_gen(settings.seed + thread_id)
    {
    }

    bool insert() { return m_rand_bool(m_gen); }

private:
    std::mt19937 m_gen;
    packed_uniform_bool_distribution m_rand_bool;
};

class workload_split {
public:
    workload_split(const struct settings &,
                   const int thread_id) :
        m_thread_id(thread_id)
    {
    }

    bool insert() const { return ((m_thread_id & 1) == 0); }

private:
    const int m_thread_id;
};

class workload_producer {
public:
    workload_producer(const struct settings &,
                      const int thread_id) :
        m_thread_id(thread_id)
    {
    }

    bool insert() const { return (m_thread_id == 0); }

private:
    const int m_thread_id;
};

class workload_alternating {
public:
    workload_alternating(const struct settings &,
                         const int) :
        m_insert(0)
    {
    }

    bool insert() { return ((m_insert++ & 1) == 1); }

private:
    uint32_t m_insert;
};

class keygen_uniform {
public:
    keygen_uniform(const struct settings &settings,
                   const int thread_id) :
        m_gen(settings.seed + thread_id),
        m_rand_int(0, std::numeric_limits<uint32_t>::max())
    {
    }

    uint32_t next()
    {
        return m_rand_int(m_gen);
    }

private:
    std::mt19937 m_gen;
    std::uniform_int_distribution<uint32_t> m_rand_int;
};

class keygen_ascending {
public:
    keygen_ascending(const struct settings &settings,
                     const int thread_id) :
        m_gen(settings.seed + thread_id),
        m_rand_int(0, UPPER_BOUND),
        m_base(0)
    {
    }

    uint32_t next()
    {
        return m_rand_int(m_gen) + m_base++;
    }

private:
    static constexpr uint32_t UPPER_BOUND = 512;

    std::mt19937 m_gen;
    std::uniform_int_distribution<uint32_t> m_rand_int;
    uint32_t m_base;
};

class keygen_descending {
public:
    keygen_descending(const struct settings &settings,
                      const int thread_id) :
        m_gen(settings.seed + thread_id),
        m_rand_int(0, UPPER_BOUND),
        m_base(0)
    {
    }

    uint32_t next()
    {
        return MAX - m_rand_int(m_gen) - m_base++;
    }

private:
    static constexpr uint32_t UPPER_BOUND = 512;
    static constexpr uint32_t MAX = std::numeric_limits<uint32_t>::max();

    std::mt19937 m_gen;
    std::uniform_int_distribution<uint32_t> m_rand_int;
    uint32_t m_base;
};

template <uint32_t UPPER_BOUND = std::numeric_limits<uint32_t>::max()>
class keygen_restricted {
public:
    keygen_restricted(const struct settings &settings,
                      const int thread_id) :
        m_gen(settings.seed + thread_id),
        m_rand_int(0, UPPER_BOUND)
    {
    }

    uint32_t next()
    {
        return m_rand_int(m_gen);
    }

private:
    std::mt19937 m_gen;
    std::uniform_int_distribution<uint32_t> m_rand_int;
};

template <class PriorityQueue, class WorkLoad, class KeyGeneration>
static void
bench_thread(PriorityQueue *pq,
             const int thread_id,
             const struct settings &settings,
             std::promise<kpq::counters> &&result)
{
#ifdef HAVE_VALGRIND
    CALLGRIND_STOP_INSTRUMENTATION;
#endif
    WorkLoad workload(settings, thread_id);
    KeyGeneration keygen(settings, thread_id);

    hwloc.pin_to_core(thread_id);

    /* The spraylist requires per-thread initialization. */
    pq->init_thread(settings.nthreads);

    /* Fill up to initial size. Do this per thread in order to build a balanced DLSM
     * instead of having one local LSM containing all initial elems. */

#ifdef ENABLE_QUALITY
    uint32_t insertion_id = 0;

    auto insertions = new std::vector<std::pair<KEY_TYPE, VAL_TYPE>>();
    auto deletions  = new std::vector<VAL_TYPE>();
    kpq::COUNTERS.insertion_sequence = insertions;
    kpq::COUNTERS.deletion_sequence  = deletions;
#endif

    const int slice_size = settings.size / settings.nthreads;
    const int initial_size = (thread_id == settings.nthreads - 1) ?
                             settings.size - thread_id * slice_size : slice_size;
    for (int i = 0; i < initial_size; i++) {
        uint32_t elem = keygen.next();
#ifdef ENABLE_QUALITY
        VAL_TYPE v = { (uint32_t)thread_id, insertion_id++, rdtsc() };
        insertions->emplace_back(elem, v);
        pq->insert(elem, v);
#else
        pq->insert(elem, elem);
#endif
    }
    fill_barrier.fetch_sub(1, std::memory_order_relaxed);

#ifdef HAVE_VALGRIND
    CALLGRIND_START_INSTRUMENTATION;
#endif

    while (!start_barrier.load(std::memory_order_relaxed)) {
        /* Wait. */
    }

#ifdef HAVE_VALGRIND
    CALLGRIND_ZERO_STATS;
#endif

    KEY_TYPE k;
    VAL_TYPE v;
    while (!end_barrier.load(std::memory_order_relaxed)) {
        if (workload.insert()) {
            k = keygen.next();
#ifdef ENABLE_QUALITY
            v = { (uint32_t)thread_id, insertion_id++, rdtsc() };
            insertions->emplace_back(k, v);
            pq->insert(k, v);
#else
            pq->insert(k, k);
#endif
            kpq::COUNTERS.inserts++;
        } else {
            if (pq->delete_min(v)) {
#ifdef ENABLE_QUALITY
                deletions->emplace_back(packed_item_id { v.thread_id
                                                       , v.element_id
                                                       , rdtsc()
                                                       });
#endif
                kpq::COUNTERS.successful_deletes++;
            } else {
                kpq::COUNTERS.failed_deletes++;
            }
        }
    }

#ifdef HAVE_VALGRIND
    CALLGRIND_STOP_INSTRUMENTATION;
#endif

    result.set_value(kpq::COUNTERS);
}

#ifdef ENABLE_QUALITY
typedef std::pair<uint32_t, uint64_t> sort_t; // <Incoming vector #, timestamp>.
typedef std::vector<std::pair<KEY_TYPE, VAL_TYPE>> insertion_sequence_t;
typedef std::vector<VAL_TYPE> deletion_sequence_t;

struct sort_greater {
    bool operator()(const sort_t &l, const sort_t &r) {
        return (l.second > r.second);  // Sort by timestamp.
    }
};

static void
sort_insertion_sequence(const std::vector<void *> &insertion_sequences,
                        insertion_sequence_t &global_insertion_sequence)
{
    std::vector<uint32_t> sort_ixs(insertion_sequences.size(), 0);

    /* Create the sorting priority queue and fill it with initial list heads. */
    std::priority_queue<sort_t, std::vector<sort_t>, sort_greater> sort_pq;
    for (uint32_t i = 0; i < insertion_sequences.size(); i++) {
        auto v = (insertion_sequence_t *)insertion_sequences[i];
        if (v->empty()) {
            continue;
        }
        const auto val = v->front().second;
        sort_pq.emplace(i, val.tick);
    }

    while (!sort_pq.empty()) {
        auto next = sort_pq.top();
        sort_pq.pop();

        const auto next_v_ix = sort_ixs[next.first]++;
        const auto next_v = (insertion_sequence_t *)insertion_sequences[next.first];
        global_insertion_sequence.push_back(next_v->at(next_v_ix));

        if (next_v_ix + 1 < next_v->size()) {
            sort_pq.emplace(next.first, next_v->at(next_v_ix + 1).second.tick);
        }
    }
}

static void
sort_deletion_sequence(const std::vector<void *> &deletion_sequences,
                       deletion_sequence_t &global_deletion_sequence)
{
    std::vector<uint32_t> sort_ixs(deletion_sequences.size(), 0);

    /* Create the sorting priority queue and fill it with initial list heads. */
    std::priority_queue<sort_t, std::vector<sort_t>, sort_greater> sort_pq;
    for (uint32_t i = 0; i < deletion_sequences.size(); i++) {
        auto v = (deletion_sequence_t *)deletion_sequences[i];
        if (v->empty()) {
            continue;
        }
        const auto val = v->front();
        sort_pq.emplace(i, val.tick);
    }

    while (!sort_pq.empty()) {
        auto next = sort_pq.top();
        sort_pq.pop();

        const auto next_v_ix = sort_ixs[next.first]++;
        const auto next_v = (deletion_sequence_t *)deletion_sequences[next.first];
        global_deletion_sequence.push_back(next_v->at(next_v_ix));

        assert(next_v_ix < next_v->size());

        if (next_v_ix + 1 < next_v->size()) {
            sort_pq.emplace(next.first, next_v->at(next_v_ix + 1).tick);
        }
    }
}

static void
evaluate_quality(std::vector<void *> &insertion_sequences,
                 std::vector<void *> &deletion_sequences,
                 double *mean,
                 uint64_t *max,
                 double *stddev)
{
    /* Merge all insertions and deletions into global sequences. The insertion
     * sequence is used to look up inserted keys later on. */

    insertion_sequence_t global_insertion_sequence;
    sort_insertion_sequence(insertion_sequences, global_insertion_sequence);

    deletion_sequence_t global_deletion_sequence;
    sort_deletion_sequence(deletion_sequences, global_deletion_sequence);

    for (auto ptr : deletion_sequences) {
        auto v = (deletion_sequence_t *)ptr;
        delete v;
    }

    /* Iterate through the sequences. For each timestamp, do insertions first
     * and then deletions, emulating each step on a sequential priority queue
     * and determining the rank error. */

    if (global_deletion_sequence.empty()) {
        *mean   = 0.;
        *max    = 0;
        *stddev = 0.;
        return;
    }

    assert(!global_deletion_sequence.empty() && !global_insertion_sequence.empty());

    uint64_t next_ins_tick = global_insertion_sequence.front().second.tick;
    uint32_t ins_ix = 0;
    uint64_t next_del_tick = global_deletion_sequence.front().tick;
    uint32_t del_ix = 0;
    assert(next_ins_tick < next_del_tick);

    typedef std::pair<KEY_TYPE, VAL_TYPE> elem_t;
    struct elem_less {
        bool operator()(const elem_t &l, const elem_t &r) {
            return (l.first < r.first);
        }
    };

    uint64_t rank_sum = 0;
    uint64_t rank_max = 0;
    std::vector<uint64_t> ranks;

    const uint64_t insertion_count = global_insertion_sequence.size();
    const uint64_t deletion_count = global_deletion_sequence.size();

    bool keep_running = true;
    kpqbench::itree pq;
    while (keep_running) {
        assert(ins_ix < insertion_count);
        assert(next_ins_tick <= next_del_tick);

        /* Do insertions. */
        while (ins_ix < insertion_count && next_ins_tick <= next_del_tick) {
            const auto elem = global_insertion_sequence[ins_ix++];
            pq.insert({ elem.first, elem.second.thread_id, elem.second.element_id });

            if (ins_ix >= insertion_count) {
                next_ins_tick = std::numeric_limits<uint64_t>::max();
                break;
            }

            next_ins_tick = global_insertion_sequence[ins_ix].second.tick;
        }

        /* Do deletions. */
        while (next_del_tick < next_ins_tick) {
            const auto deleted_item = global_deletion_sequence[del_ix++];

            /* Look up the key. */
            auto insertions = (insertion_sequence_t *)insertion_sequences[deleted_item.thread_id];
            const KEY_TYPE key = insertions->at(deleted_item.element_id).first;

            uint64_t rank;
            pq.erase({ key, deleted_item.thread_id, deleted_item.element_id }, &rank);

            ranks.push_back(rank);
            rank_sum += rank;
            rank_max = std::max(rank_max, rank);

            if (del_ix >= deletion_count) {
                keep_running = false;
                break;
            }

            next_del_tick = global_deletion_sequence[del_ix].tick;
        }
    }

    /* Clean up the insertion sequence. */

    for (auto ptr : insertion_sequences) {
        auto v = (insertion_sequence_t *)ptr;
        delete v;
    }

    const double rank_mean = (double)rank_sum / ranks.size();

    double rank_squared_difference = 0;
    for (uint64_t rank : ranks) {
        rank_squared_difference += std::pow(rank - rank_mean, 2);
    }

    const double rank_stddev = std::sqrt(rank_squared_difference / ranks.size());

    *mean   = rank_mean;
    *max    = rank_max;
    *stddev = rank_stddev;
}
#endif

template <class PriorityQueue>
static int
bench(PriorityQueue *pq,
      const struct settings &settings)
{
    if (settings.nthreads > 1 && !pq->supports_concurrency()) {
        fprintf(stderr, "The given data structure does not support concurrency.\n");
        return -1;
    }

    int ret = 0;

    fill_barrier.store(settings.nthreads, std::memory_order_relaxed);

    /* Start all threads. */

    auto fn = bench_thread<PriorityQueue, workload_uniform, keygen_uniform>;
    switch (settings.workload) {
    case WORKLOAD_UNIFORM:
        switch (settings.keys) {
        case KEYS_UNIFORM:
            fn = bench_thread<PriorityQueue, workload_uniform, keygen_uniform>; break;
        case KEYS_ASCENDING:
            fn = bench_thread<PriorityQueue, workload_uniform, keygen_ascending>; break;
        case KEYS_DESCENDING:
            fn = bench_thread<PriorityQueue, workload_uniform, keygen_descending>; break;
        case KEYS_RESTRICTED_8:
            fn = bench_thread<PriorityQueue, workload_uniform, keygen_restricted<1 << 8>>; break;
        case KEYS_RESTRICTED_16:
            fn = bench_thread<PriorityQueue, workload_uniform, keygen_restricted<1 << 16>>; break;
        default: assert(false);
        }
        break;
    case WORKLOAD_SPLIT:
        switch (settings.keys) {
        case KEYS_UNIFORM:
            fn = bench_thread<PriorityQueue, workload_split, keygen_uniform>; break;
        case KEYS_ASCENDING:
            fn = bench_thread<PriorityQueue, workload_split, keygen_ascending>; break;
        case KEYS_DESCENDING:
            fn = bench_thread<PriorityQueue, workload_split, keygen_descending>; break;
        case KEYS_RESTRICTED_8:
            fn = bench_thread<PriorityQueue, workload_split, keygen_restricted<1 << 8>>; break;
        case KEYS_RESTRICTED_16:
            fn = bench_thread<PriorityQueue, workload_split, keygen_restricted<1 << 16>>; break;
        default: assert(false);
        }
        break;
    case WORKLOAD_PRODUCER:
        switch (settings.keys) {
        case KEYS_UNIFORM:
            fn = bench_thread<PriorityQueue, workload_producer, keygen_uniform>; break;
        case KEYS_ASCENDING:
            fn = bench_thread<PriorityQueue, workload_producer, keygen_ascending>; break;
        case KEYS_DESCENDING:
            fn = bench_thread<PriorityQueue, workload_producer, keygen_descending>; break;
        case KEYS_RESTRICTED_8:
            fn = bench_thread<PriorityQueue, workload_producer, keygen_restricted<1 << 8>>; break;
        case KEYS_RESTRICTED_16:
            fn = bench_thread<PriorityQueue, workload_producer, keygen_restricted<1 << 16>>; break;
        default: assert(false);
        }
        break;
    case WORKLOAD_ALTERNATING:
        switch (settings.keys) {
        case KEYS_UNIFORM:
            fn = bench_thread<PriorityQueue, workload_alternating, keygen_uniform>; break;
        case KEYS_ASCENDING:
            fn = bench_thread<PriorityQueue, workload_alternating, keygen_ascending>; break;
        case KEYS_DESCENDING:
            fn = bench_thread<PriorityQueue, workload_alternating, keygen_descending>; break;
        case KEYS_RESTRICTED_8:
            fn = bench_thread<PriorityQueue, workload_alternating, keygen_restricted<1 << 8>>; break;
        case KEYS_RESTRICTED_16:
            fn = bench_thread<PriorityQueue, workload_alternating, keygen_restricted<1 << 16>>; break;
        default: assert(false);
        }
        break;
    default: assert(false);
    }

    std::vector<std::future<kpq::counters>> futures;
    std::vector<std::thread> threads(settings.nthreads);
    for (int i = 0; i < settings.nthreads; i++) {
        std::promise<kpq::counters> p;
        futures.push_back(p.get_future());
        threads[i] = std::thread(fn, pq, i, settings, std::move(p));
    }

    /* Wait until threads are done filling their queue. */
    while (fill_barrier.load(std::memory_order_relaxed) > 0) {
        /* Wait. */
    }

    /* Begin benchmark. */
    start_barrier.store(true, std::memory_order_relaxed);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    usleep(1000000 * DEFAULT_SLEEP);
    end_barrier.store(true, std::memory_order_relaxed);

    clock_gettime(CLOCK_MONOTONIC, &end);
    /* End benchmark. */

    for (auto &thread : threads) {
        thread.join();
    }

#ifdef ENABLE_QUALITY
    std::vector<void *> insertion_sequences;
    std::vector<void *> deletion_sequences;
#endif

    kpq::counters counters;
    for (auto &future : futures) {
        auto counter = future.get();
        counters += counter;
#ifdef ENABLE_QUALITY
        insertion_sequences.push_back(counter.insertion_sequence);
        deletion_sequences.push_back(counter.deletion_sequence);
#endif
    }

#ifdef ENABLE_QUALITY
    uint64_t max;
    double mean, stddev;
    evaluate_quality(insertion_sequences, deletion_sequences, &mean, &max, &stddev);
    fprintf(stdout, "%f, %lu, %f\n", mean, max, stddev);
#else
    const double elapsed = timediff_in_s(start, end);
    size_t ops_per_s = (size_t)((double)counters.operations() / elapsed);

    fprintf(stdout, "%zu\n", ops_per_s);
#endif

    if (settings.print_counters) {
        counters.print();
    }

    return ret;
}

static int
safe_parse_int_arg(const char *arg)
{
    errno = 0;
    const int i = strtol(arg, NULL, 0);
    if (errno != 0) {
        usage();
    }
    return i;
}

int
main(int argc,
     char **argv)
{
    int ret = 0;
    struct settings settings = { DEFAULT_NTHREADS
                               , DEFAULT_SEED
                               , DEFAULT_SIZE
                               , ""
                               , DEFAULT_COUNTERS
                               , DEFAULT_KEYS
                               , DEFAULT_WORKLOAD
                               };

    int opt;
    while ((opt = getopt(argc, argv, "ci:k:n:p:s:w:")) != -1) {
        switch (opt) {
        case 'c':
            settings.print_counters = true;
            break;
        case 'i':
            settings.size = safe_parse_int_arg(optarg);
            break;
        case 'k':
            settings.keys = safe_parse_int_arg(optarg);
            break;
        case 'p':
            settings.nthreads = safe_parse_int_arg(optarg);
            break;
        case 's':
            settings.seed = safe_parse_int_arg(optarg);
            break;
        case 'w':
            settings.workload = safe_parse_int_arg(optarg);
            break;
        default:
            usage();
        }
    }

    settings.type = argv[optind];
    if (!settings.are_valid()) {
        usage();
    }

    if (optind != argc - 1) {
        usage();
    }

    if (settings.type == PQ_CHEAP) {
        kpqbench::cheap<KEY_TYPE, VAL_TYPE> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_DLSM) {
        kpq::dist_lsm<KEY_TYPE, VAL_TYPE, DEFAULT_RELAXATION> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_GLOBALLOCK) {
        kpqbench::GlobalLock<KEY_TYPE, VAL_TYPE> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_KLSM16) {
        kpq::k_lsm<KEY_TYPE, VAL_TYPE, 16> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_KLSM128) {
        kpq::k_lsm<KEY_TYPE, VAL_TYPE, 128> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_KLSM256) {
        kpq::k_lsm<KEY_TYPE, VAL_TYPE, 256> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_KLSM4096) {
        kpq::k_lsm<KEY_TYPE, VAL_TYPE, 4096> pq;
        ret = bench(&pq, settings);
#ifndef ENABLE_QUALITY
    } else if (settings.type == PQ_LSM) {
        kpq::LSM<KEY_TYPE> pq;
        ret = bench(&pq, settings);
#endif
    } else if (settings.type == PQ_MLSM) {
        kpq::multi_lsm<KEY_TYPE, VAL_TYPE> pq(settings.nthreads);
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_MULTIQ) {
        kpqbench::multiq<KEY_TYPE, VAL_TYPE> pq(settings.nthreads);
        ret = bench(&pq, settings);
#ifndef ENABLE_QUALITY
    } else if (settings.type == PQ_SEQUENCE) {
        kpqbench::sequence_heap<KEY_TYPE> pq;
        ret = bench(&pq, settings);
    } else if (settings.type == PQ_SKIP) {
        kpqbench::skip_queue<KEY_TYPE> pq;
        ret = bench(&pq, settings);
#endif
    } else if (settings.type == PQ_SLSM) {
        kpq::shared_lsm<KEY_TYPE, VAL_TYPE, DEFAULT_RELAXATION> pq;
        ret = bench(&pq, settings);
    } else {
        usage();
    }

    return ret;
}
