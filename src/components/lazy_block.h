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

#include <cassert>

#include "block.h"

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
    lazy_block(const block<K, V> *b,
               const size_t b_first);
    virtual ~lazy_block();

    void merge(const block<K, V> *b,
               const size_t b_first);

    block<K, V> *finalize();

    size_t power_of_2() const { return m_power_of_2; }
    size_t capacity() const { return m_capacity; }

private:
    size_t m_power_of_2;
    size_t m_capacity;

    const block<K, V> *m_blocks[MaxBlocks];
    size_t m_firsts[MaxBlocks];
    size_t m_block_count;
};

#include "lazy_block_inl.h"

}

#endif /* __LAZY_BLOCK_H */
