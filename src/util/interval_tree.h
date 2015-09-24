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

#ifndef __INTERVAL_TREE_H
#define __INTERVAL_TREE_H

#include "mm.h"

extern "C" {
#include "itree.h"
}

namespace kpq {

class interval_tree
{
public:
    void insert(const uint64_t index) {
        uint64_t dummy;
        const int succeeded = itree_insert(index, &m_root, &dummy);
        assert(succeeded), (void)succeeded;
    }

    uint64_t count_before(const uint64_t index) const;

    void clear() {
        postorder_free(m_root);
        m_root = nullptr;
    }

    struct reuse {
        bool operator()(const itree_t &t) const {
            return !t.in_use;
        }
    };

private:
    void postorder_free(itree_t *t) {
        if (t == nullptr) {
            return;
        }

        postorder_free(t->l);
        postorder_free(t->r);
        t->in_use = false;
    }

private:
    itree_t *m_root;

    item_allocator<itree_t, reuse, 32> m_allocator;
};

} // namespace kpq

#endif /*  __INTERVAL_TREE_H */
