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

template <class K, class V, int Rlx>
dist_lsm_local<K, V, Rlx>::dist_lsm_local() :
    m_blocks { nullptr },
    m_size(0),
    m_cached_best(block<K, V>::peek_t::EMPTY())
{
}

template <class K, class V, int Rlx>
dist_lsm_local<K, V, Rlx>::~dist_lsm_local()
{
    /* Blocks and items are managed by, respectively,
     * block_storage and item_allocator. */
}

template <class K, class V, int Rlx>
void
dist_lsm_local<K, V, Rlx>::insert(const K &key,
                                  const V &val,
                                  shared_lsm<K, V, Rlx> *slsm)
{
    item<K, V> *it = m_item_allocator.acquire();
    it->initialize(key, val);

    insert(it, it->version(), slsm);
}

template <class K, class V, int Rlx>
void
dist_lsm_local<K, V, Rlx>::insert(item<K, V> *it,
                                  const version_t version,
                                  shared_lsm<K, V, Rlx> *slsm)
{
    const K it_key = it->key();

    /* Update the cached best item if necessary. */

    if (m_cached_best.empty() || it_key < m_cached_best.m_key) {
        m_cached_best.m_key     = it_key;
        m_cached_best.m_item    = it;
        m_cached_best.m_version = version;
    } else if (m_cached_best.taken()) {
        m_cached_best.m_item    = nullptr;
    }

    /* If possible, simply append to the current tail block. */

    const bool empty = (m_size == 0);
    auto tail = empty ? nullptr : m_blocks[m_size - 1];

    if (!empty) {
        if (tail->last() < tail->capacity()) {
            K tail_key;
            if (tail->peek_tail(tail_key) && tail_key <= it_key) {
                tail->insert_tail(it, version);
                return;
            }
        }
    }

    /* Allocate the biggest possible array. This is an optimization
     * only. For correctness, it is enough to always allocate a new
     * array of capacity 1. */

    block<K, V> *new_block = m_block_storage.get_block(0);
    new_block->insert(it, version);

    merge_insert(new_block, slsm);
}

template <class K, class V, int Rlx>
void
dist_lsm_local<K, V, Rlx>::merge_insert(block<K, V> *const new_block,
                                        shared_lsm<K, V, Rlx> *slsm)
{
    int other_ix = m_size - 1;
    const size_t old_size = m_size;

    block<K, V> *insert_block = new_block;
    block<K, V> *other_block  = (other_ix < 0) ? nullptr : m_blocks[other_ix];
    block<K, V> *delete_block = nullptr;

    /* Merge as long as the prev block is of the same size as the new block. */
    while (other_block != nullptr && insert_block->capacity() == other_block->capacity()) {
        /* Only merge into a larger block if both candidate blocks have enough elements to
         * justify the larger size. This change is necessary to avoid huge blocks containing
         * only a few elements (which actually happens with the 'alloc largest block on insert'
         * optimization. */
        const size_t merged_pow2 =
            (insert_block->size() + other_block->size() <= insert_block->capacity()) ?
            insert_block->power_of_2() : insert_block->power_of_2() + 1;
        auto merged_block = m_block_storage.get_block(merged_pow2);
        merged_block->merge(insert_block, other_block);

        insert_block->set_unused();
        insert_block = merged_block;
        delete_block = other_block;

        other_ix--;
        other_block  = (other_ix < 0) ? nullptr : m_blocks[other_ix];
    }

    if (slsm != nullptr && insert_block->size() >= (Rlx + 1) / 2) {
        /* The merged block exceeds relaxation bounds and we have a shared lsm
         * pointer, insert the new block into the shared lsm instead.
         * The shared lsm creates a copy of the passed block, and thus we can set
         * the passed block unused once insertion has completed.
         *
         * TODO: Optimize this by allocating the block from the shared lsm
         * if we are about to merge into a block exceeding the relaxation bound.
         */
        slsm->insert(insert_block);
        insert_block->set_unused();

        m_size = other_ix + 1;
    } else {
        /* Insert the new block into the list. */
        m_blocks[other_ix + 1] = insert_block;
        m_size = other_ix + 2;
    }

    /* Remove merged blocks from the list. */
    if (delete_block != nullptr) delete_block->set_unused();
    for (size_t i = m_size; i < old_size; i++) {
        m_blocks[i]->set_unused();
    }
}

template <class K, class V, int Rlx>
bool
dist_lsm_local<K, V, Rlx>::delete_min(dist_lsm<K, V, Rlx> *parent,
                                      V &val)
{
    typename block<K, V>::peek_t best = block<K, V>::peek_t::EMPTY();
    peek(best);

    if (best.m_item == nullptr && spy(parent) > 0) {
        peek(best); /* Retry once after a successful spy(). */
    }

    if (best.m_item == nullptr) {
        return false; /* We did our best, give up. */
    }

    return best.m_item->take(best.m_version, val);
}

template <class K, class V, int Rlx>
void
dist_lsm_local<K, V, Rlx>::peek(typename block<K, V>::peek_t &best)
{
    /* Short-circuit. */
    if (!m_cached_best.empty() && !m_cached_best.taken()) {
        best = m_cached_best;
        return;
    }

    for (size_t ix = 0; ix < m_size; ix++) {
outer:
        auto i = m_blocks[ix];
        auto candidate = i->peek();

        while (i->size() <= i->capacity() / 2) {

            /* Simply remove empty blocks. */
            if (i->size() == 0) {
                memmove(&m_blocks[ix],
                        &m_blocks[ix + 1],
                        sizeof(m_blocks[0]) * (m_size - ix - 1));
                m_size--;
                i->set_unused();

                goto outer;
            }

            /* Shrink. */

            block<K, V> *new_block = m_block_storage.get_block(i->power_of_2() - 1);
            new_block->copy(i);
            i->set_unused();

            /* Merge. TODO: Shrink-Merge optimization. */

            size_t next_ix = ix + 1;
            auto next = m_blocks[next_ix];
            if (next_ix < m_size && new_block->capacity() == next->capacity()) {
                auto merged_block = m_block_storage.get_block(new_block->power_of_2() + 1);
                merged_block->merge(new_block, next);

                next->set_unused();
                new_block->set_unused();
                new_block = merged_block;

                memmove(&m_blocks[next_ix],
                        &m_blocks[next_ix + 1],
                        sizeof(m_blocks[0]) * (m_size - next_ix - 1));
                m_size--;
            }

            /* Insert new block. */

            m_blocks[ix] = new_block;

            /* Bookkeeping and rerun peek(). */

            i = new_block;
            candidate = i->peek();
        }

        if (best.empty() || (!candidate.empty() && candidate.m_key < best.m_key)) {
            best = candidate;
        }
    }

    m_cached_best = best;
}

template <class K, class V, int Rlx>
int
dist_lsm_local<K, V, Rlx>::spy(dist_lsm<K, V, Rlx> *parent)
{
    (void)parent;
    return 0;

    // TODO: Reimplement spy in a performant and scalable way. The current implementation
    // assumed that it would be called infrequently, and thus was half-intentionally
    // tolerated even though it's extremely inefficient. Disable spying for now until
    // we have a fast alternative.

#if 0
    int num_spied = 0;

    const size_t num_threads    = parent->m_local.num_threads();
    const size_t current_thread = parent->m_local.current_thread();

    if (num_threads < 2) {
        return num_spied;
    }

    /* All except current thread, therefore n - 2. */
    std::uniform_int_distribution<> rand_int(0, num_threads - 2);
    size_t victim_id = rand_int(m_gen);
    if (victim_id >= current_thread) {
        victim_id++;
    }

    auto victim = parent->m_local.get(victim_id);
    for (size_t ix = 0; ix < victim->m_size; ix++) {
        const auto b = victim->m_blocks[ix];

        block<K, V> *new_block = m_block_storage.get_block(b->power_of_2());
        new_block->copy(b);

        // TODO: Verify that b is unchanged, i.e. we have a consistent copy.

        num_spied += new_block->size();

        m_blocks[m_size++] = new_block;
    }

    return num_spied;
#endif
}

template <class K, class V, int Rlx>
void
dist_lsm_local<K, V, Rlx>::print() const
{
    m_block_storage.print();
}
