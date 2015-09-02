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

template <class K, class V, int MaxBlocks>
lazy_block<K, V, MaxBlocks>::lazy_block(const block<K, V> *b,
                                        const size_t b_first) :
    m_power_of_2(b->power_of_2()),
    m_capacity(b->capacity()),
    m_block_count(1)
{
    m_blocks[0] = b;
    m_firsts[0] = b_first;
}

template <class K, class V, int MaxBlocks>
lazy_block<K, V, MaxBlocks>::~lazy_block()
{
}

template <class K, class V, int MaxBlocks>
void
lazy_block<K, V, MaxBlocks>::merge(const block<K, V> *b,
                                   const size_t b_first)
{
    assert(m_block_count < MaxBlocks);
    assert(m_power_of_2 == b->power_of_2());

    m_blocks[m_block_count] = b;
    m_firsts[m_block_count] = b_first;
    m_block_count++;

    m_power_of_2 += 1;
    m_capacity  <<= 1;
}

template <class K, class V, int MaxBlocks>
block<K, V> *
lazy_block<K, V, MaxBlocks>::finalize()
{
    return nullptr;
}
