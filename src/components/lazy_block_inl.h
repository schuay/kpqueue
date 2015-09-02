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
lazy_block<K, V, MaxBlocks>::lazy_block(block<K, V> *b,
                                        const size_t b_first) :
    m_power_of_2(b->power_of_2()),
    m_capacity(b->capacity()),
    m_block_count(0)
{
    if (next_head(b, b_first, m_heads[0])) {
        m_block_count++;
    }
}

template <class K, class V, int MaxBlocks>
lazy_block<K, V, MaxBlocks>::~lazy_block()
{
}

template <class K, class V, int MaxBlocks>
void
lazy_block<K, V, MaxBlocks>::merge(block<K, V> *b,
                                   const size_t b_first)
{
    assert(m_block_count < MaxBlocks);
    assert(m_power_of_2 == b->power_of_2());

    if (next_head(b, b_first, m_heads[m_block_count])) {
        m_block_count++;
    }

    m_power_of_2 += 1;
    m_capacity  <<= 1;
}

template <class K, class V, int MaxBlocks>
bool
lazy_block<K, V, MaxBlocks>::next_head(block<K, V> *b,
                                       const size_t ix,
                                       typename lazy_block<K, V, MaxBlocks>::block_head &head)
{
    size_t i = ix;
    const size_t last = b->last();
    while (!b->item_owned(b->m_item_pairs[i]) && i < last) {
        i++;
    }

    if (ix < b->last()) {
        head.b = b;
        head.ix = i;
        head.key = b->m_item_pairs[i].first->key();
        return true;
    }

    return false;
}

template <class K, class V, int MaxBlocks>
block<K, V> *
lazy_block<K, V, MaxBlocks>::finalize(block_pool<K, V> *pool)
{
    if (m_block_count == 1) {
        return m_heads[0].b;
    }

    /* Perform a multi-way merge of the given blocks. */

    auto merge_block = pool->get_block(m_power_of_2);
    std::make_heap(m_heads, m_heads + m_block_count);

    int dst = 0;
    while (m_block_count > 1) {
        std::pop_heap(m_heads, m_heads + m_block_count);
        m_block_count--;

        block_head &head = m_heads[m_block_count];
        merge_block->m_item_pairs[dst++] = head.b->m_item_pairs[head.ix];

        if (next_head(head.b, head.ix + 1, head)) {
            m_block_count++;
            std::push_heap(m_heads, m_heads + m_block_count);
        }
    }

    // Append all trailing items in remaining block.

    block_head &head = m_heads[0];
    do {
        merge_block->m_item_pairs[dst++] = head.b->m_item_pairs[head.ix];
    } while (next_head(head.b, head.ix + 1, head));

    merge_block->m_last = dst;

    return merge_block;
}
