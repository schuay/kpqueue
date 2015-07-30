/*
 *  Copyright 2015 Jakob Gruber
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

#ifndef __SHARED_LSM_LOCAL_H
#define __SHARED_LSM_LOCAL_H

#include <atomic>

#include "util/mm.h"
#include "block_array.h"
#include "block_pool.h"

namespace kpq {

template <class K, class V, int Relaxation>
class shared_lsm_local {
public:
    shared_lsm_local();
    virtual ~shared_lsm_local() { }

private:
    /* ---- Item memory management. ---- */

    item_allocator<item<K, V>, typename item<K, V>::reuse> m_item_pool;

    /* ---- Block memory management. ---- */

    block_pool<K, V> m_block_pool;

    /* ---- Block array memory management. ---- */

    /** Contains a copy of the global block array, updated regularly. */
    block_array<K, V> m_local_array_copy;

    /** Local memory pools for use by block arrays. */
    block_array<K, V> m_array_pool_odds;
    block_array<K, V> m_array_pool_evens;
};

#include "shared_lsm_local_inl.h"

}

#endif /* __SHARED_LSM_LOCAL_H */
