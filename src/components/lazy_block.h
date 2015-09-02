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

#ifndef __LAZY_BLOCK_H
#define __LAZY_BLOCK_H

#include <algorithm>
#include <cassert>

#include "block.h"
#include "shared_lsm/block_pool.h"

namespace kpq
{

/**
 * Performs lazy merges on the given blocks, i.e. given blocks
 * are simply added to a list upon merge(), and only physically
 * merged in finalize().
 */

template <class K, class V, int MaxBlocks>
class lazy_block
{
public:
    lazy_block(block<K, V> *b,
               const size_t b_first);
    virtual ~lazy_block();

    void merge(block<K, V> *b,
               const size_t b_first);

    // TODO: Using the slsm pool here is less than ideal. A consistent interface
    // between block_pool and block_storage would be great.
    block<K, V> *finalize(block_pool<K, V> *pool);

    size_t power_of_2() const { return m_power_of_2; }
    size_t capacity() const { return m_capacity; }

private:
    struct block_head
    {
        block<K, V> *b;
        size_t ix;
        K key;

        bool operator<(const block_head &that) const {
            // Note the reversed operator used in order to create a min heap.
            return this->key > that.key;
        }
    };

private:
    static bool next_head(block<K, V> *b,
                          const size_t ix,
                          block_head &head);

private:
    size_t m_power_of_2;
    size_t m_capacity;

    block_head m_heads[MaxBlocks];
    size_t m_block_count;
};

#include "lazy_block_inl.h"

}

#endif /* __LAZY_BLOCK_H */
