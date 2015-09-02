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

#ifndef __BLOCK_H
#define __BLOCK_H

#include <atomic>
#include <cassert>
#include <utility>

#include "util/thread_local_ptr.h"
#include "item.h"

namespace kpq
{

/**
 * A block stores references to items together with their expected version.
 * An item is owned by this block if its version is equal to the expected version,
 * otherwise it has been processed by another thread and possibly reused.
 *
 * A block is always of capacity 2^i, i \in N_0. For all owned items, if the index i < j
 * then i.key < j.key.
 */

template <class K, class V>
class block
{
private:
    template <class X, class Y, int Z>
    friend class lazy_block;

    typedef std::pair<item<K, V> *, version_t> item_pair_t;

public:
    /** Information about a specific item. A nullptr item denotes failure of the operation. */
    struct peek_t {
        peek_t() : m_item(nullptr), m_version(0) { }

        bool taken() const { return m_item->version() != m_version; }
        bool empty() const { return (m_item == nullptr); }
        bool take(V &val) { return m_item->take(m_version, val); }

        K m_key;
        item<K, V> *m_item;
        size_t m_index;  /**< The item's index within the block. */
        version_t m_version;
    };

    class spying_iterator
    {
        friend class block<K, V>;
    public:
        peek_t next();

    private:
        item_pair_t *m_item_pairs;
        size_t m_last, m_next;
    };

public:
    block(const size_t power_of_2);
    virtual ~block();

    void insert(item<K, V> *it,
                const version_t version);
    void insert_tail(item<K, V> *it,
                     const version_t version);
    void merge(const block<K, V> *lhs,
               const block<K, V> *rhs);
    void merge(const block<K, V> *lhs,
               const size_t lhs_first,
               const block<K, V> *rhs,
               const size_t rhs_first);
    void copy(const block<K, V> *that);

    /** Returns null if the block is empty, and a peek_t struct of the minimal item
     *  otherwise. Removes observed unowned items from the current block. */
    peek_t peek();

    /** Iterates the block from last to first and sets key to the first key it finds.
     *  If none are found, returns false. */
    bool peek_tail(K &key);

    /** Returns the n-th item within this block (i.e. items[n]). */
    peek_t peek_nth(const size_t n);

    spying_iterator iterator();

    size_t first() const;
    size_t last() const;
    size_t size() const;
    size_t power_of_2() const;
    size_t capacity() const;

    bool used() const;
    void set_unused();
    void set_used();

    void clear();

public:
    /** Next pointers may be used by all threads. */
    std::atomic<block<K, V> *> m_next;
    /** Prev pointers may be used only by the owning thread. */
    block<K, V> *m_prev;

private:
    static bool item_owned(const item_pair_t &item_pair);

private:
    /** Points to the lowest known filled index. */
    size_t m_first;

    /** Points to the highest known filled index + 1.
     *  Since the dist LSM is concurrent and other threads can take items without the owning
     *  thread knowing about it, size is not an exact value. Instead, it counts the number
     *  of elements that were written into the local list of items by the owning thread,
     *  even if those items currently aren't active anymore. */
    size_t m_last;

    /** The capacity stored as a power of 2. */
    const size_t m_power_of_2;
    const size_t m_capacity;

    const int32_t m_owner_tid;

    item_pair_t *m_item_pairs;

    /** Specifies whether the block is currently in use. */
    bool m_used;
};

#include "block_inl.h"

}

#endif /* __BLOCK_H */
