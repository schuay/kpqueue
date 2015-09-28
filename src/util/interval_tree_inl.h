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
    const bool succeeded = (itree_insert(index, &m_root) == 0);
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
    itree_free(m_root);
    m_root = nullptr;
}

int
interval_tree::itree_insert(const uint64_t index,
                            itree_t **root)
{
    itree_util_t util;
    memset(&util, 0, sizeof(itree_util_t));

    int ret = _itree_insert(index, root, &util);

    if (util.l != nullptr) {
        util.l->in_use = false;
    }

    return ret;
}

int
interval_tree::_itree_new_node(const uint64_t index,
                               itree_t **root)
{
    itree_t *droot = m_allocator.acquire();
    assert (droot != nullptr);

    memset(droot, 0, sizeof(*droot));
    droot->k1 = index;
    droot->k2 = index;
    droot->in_use = true;
    *root = droot;
    return 0;
}

/**
 * Extends node by adding index to the node interval.
 *
 * Preconditions:
 *  * node != nullptr.
 *  * index is immediately adjacent to the node interval.
 *
 * Postconditions:
 *  * The node interval has been extended by index.
 */
void
interval_tree::_itree_extend_node(const uint64_t index,
                                  itree_t *node)
{
    assert(index == node->k1 - 1 || node->k2 + 1 == index);

    if (index < node->k1) {
        node->k1 = index;
    } else {
        node->k2 = index;
    }
}

/**
 * Merges lower into upper. Lower is *not* deleted.
 *
 * Preconditions:
 *  * lower, upper != nullptr.
 *  * upper->k2 + 1 == lower->k1 - 1 OR
 *    upper->k1 - 1 == lower->k2 + 1
 *
 * Postconditions:
 *  * The nodes are merged.
 */
void
interval_tree::_itree_merge_nodes(itree_t *upper,
                                  itree_t *lower)
{
    assert(upper->k2 + 1 == lower->k1 - 1 || upper->k1 - 1 == lower->k2 + 1);

    if (upper->k1 > lower->k2) {
        upper->k1 = lower->k1;
    } else {
        upper->k2 = lower->k2;
    }
}

inline int8_t
interval_tree::_itree_height(const itree_t *node)
{
    return (node == nullptr) ? -1 : node->h;
}

inline void
interval_tree::_itree_set_height(itree_t *node)
{
    node->h = std::max(_itree_height(node->l), _itree_height(node->r)) + 1;
}

/**
 * Returns the count of elements in the tree.
 */
inline uint64_t
interval_tree::_itree_count(const itree_t *root)
{
    if (root == nullptr) {
        return 0;
    }
    return root->k2 - root->k1 + 1 + root->v + _itree_count(root->r);
}

/**
 * Rebalances the subtree.
 *
 * Precondition:
 *  * root != nullptr.
 *  * The subtrees root->l and root->r are balanced.
 *
 * Postcondition:
 *  * The subtree is balanced but otherwise unchanged.
 */
void
interval_tree::_itree_rebalance(itree_t **root)
{
    itree_t *droot = *root;

    const int lh = _itree_height(droot->l);
    const int rh = _itree_height(droot->r);

    if (abs(lh - rh) < 2) {
        /* No rebalancing required. */
        return;
    }

    if (lh < rh) {
        itree_t *r = droot->r;

        const int rlh = _itree_height(r->l);
        const int rrh = _itree_height(r->r);

        /* Right-left case. */
        if (rlh > rrh) {
            droot->r = r->l;
            r->l = droot->r->r;
            droot->r->r = r;

            r->v = _itree_count(r->l);

            _itree_set_height(r);

            r = droot->r;
        }

        /* Right-right case. */

        droot->r = r->l;
        r->l = droot;
        *root = r;

        r->v += droot->v + droot->k2 - droot->k1 + 1;

        _itree_set_height(droot);
        _itree_set_height(r);
    } else {
        itree_t *l = droot->l;

        const int llh = _itree_height(l->l);
        const int lrh = _itree_height(l->r);

        /* Left-right case. */
        if (lrh > llh) {
            droot->l = l->r;
            l->r = droot->l->l;
            droot->l->l = l;

            droot->l->v += l->v + l->k2 - l->k1 + 1;

            _itree_set_height(l);

            l = droot->l;
        }

        /* Left-left case. */

        droot->l = l->r;
        l->r = droot;
        *root = l;

        droot->v = _itree_count(droot->l);

        _itree_set_height(droot);
        _itree_set_height(l);
    }
}

int
interval_tree::_itree_descend_l(const uint64_t index,
                                itree_t **root,
                                itree_util_t *util)
{
    itree_t *droot = *root;

    if (droot->k1 == index + 1) {
        if (util->u == nullptr) {
            util->u = droot;
        } else {
            util->l = droot;
        }
    }

    const int below_merge = (util->u != nullptr);

    /* Index was added as a new descendant node. */
    if (util->u == nullptr && util->u != droot) {
        droot->v++;
    }

    int ret = _itree_insert(index, &droot->l, util);
    if (ret != 0) { return ret; }

    /* Remove the lower node. */
    if (util->l != nullptr && util->l == droot->l) {
        const int in_left_subtree = (index == util->l->k2 + 1);
        droot->l = in_left_subtree ? util->l->l : util->l->r;
    }

    /* Adjust the subtree sum after a merge. */
    if (util->l != nullptr && util->l != droot && below_merge) {
        droot->v -= util->l->k2 - util->l->k1 + 1;
    }

    return 0;
}

int
interval_tree::_itree_descend_r(const uint64_t index,
                                itree_t **root,
                                itree_util_t *util)
{
    itree_t *droot = *root;

    if (droot->k2 == index - 1) {
        if (util->u == nullptr) {
            util->u = droot;
        } else {
            util->l = droot;
        }
    }

    int ret = _itree_insert(index, &droot->r, util);
    if (ret != 0) { return ret; }

    /* Remove the lower node. */
    if (util->l != nullptr && util->l == droot->r) {
        const int in_left_subtree = (index == util->l->k2 + 1);
        droot->r = in_left_subtree ? util->l->l : util->l->r;
    }

    return 0;
}

/**
 * The workhorse for itree_insert.
 * Util keeps track of several internal variables needed for merging nodes.
 */
int
interval_tree::_itree_insert(const uint64_t index,
                             itree_t **root,
                             itree_util_t *util)
{
    itree_t *droot = *root;
    int ret = 0;

    /* Merge two existing nodes. */
    if (droot == nullptr && util->l != nullptr) {
        _itree_merge_nodes(util->u, util->l);
        return 0;
    }

    /* Add to existing adjacent node. */
    if (droot == nullptr && util->u != nullptr) {
        _itree_extend_node(index, util->u);
        return 0;
    }

    /* New node. */
    if (droot == nullptr) {
        return _itree_new_node(index, root);
    }

    /* Descend into left or right subtree. */
    if (droot->k1 > index) {
        if ((ret = _itree_descend_l(index, root, util)) != 0) {
            return ret;
        }
    } else if (index > droot->k2) {
        if ((ret = _itree_descend_r(index, root, util)) != 0) {
            return ret;
        }
    } else {
        fprintf(stderr, "Index %llu is already in tree\n",
                (long long unsigned)index);
        return -1;
    }

    /* Rebalance if necessary. */
    _itree_rebalance(root);

    _itree_set_height(droot);

    return ret;
}

void
interval_tree::itree_free(itree_t *root)
{
    if (root == nullptr) {
        return;
    }

    itree_free(root->l);
    itree_free(root->r);

    root->in_use = false;
}
