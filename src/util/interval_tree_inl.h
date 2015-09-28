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

void
interval_tree::insert(const uint64_t index)
{
    uint64_t dummy;
    const int succeeded = itree_insert(index, &m_root, &dummy);
    assert(succeeded), (void)succeeded;
}

uint64_t
interval_tree::count_before(const uint64_t index) const
{
    return index;
}

void
interval_tree::clear()
{
    postorder_free(m_root);
    m_root = nullptr;
}

void
interval_tree::postorder_free(itree_t *t)
{
    if (t == nullptr) {
        return;
    }

    postorder_free(t->l);
    postorder_free(t->r);
    t->in_use = false;
}
