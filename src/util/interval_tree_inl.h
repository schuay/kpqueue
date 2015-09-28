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

int
interval_tree::itree_insert(const uint64_t index,
                            itree_t **root,
                            uint64_t *holes)
{
    itree_util_t util;
    memset(&util, 0, sizeof(itree_util_t));

    *holes = 0;

    int ret = _itree_insert(index, root, holes, &util);

    if (util.l != NULL) {
        free(util.l);
    }

    return ret;
}

int
interval_tree::_itree_new_node(const uint64_t index,
                               itree_t **root)
{
    itree_t *droot = (itree_t *)calloc(1, sizeof(itree_t));
    if (droot == NULL) {
        perror("calloc");
        return -1;
    }
    droot->k1 = index;
    droot->k2 = index;
    *root = droot;
    return 0;
}

/**
 * Extends node by adding index to the node interval.
 *
 * Preconditions:
 *  * node != NULL.
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
 *  * lower, upper != NULL.
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
    return (node == NULL) ? -1 : node->h;
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
    if (root == NULL) {
        return 0;
    }
    return root->k2 - root->k1 + 1 + root->v + _itree_count(root->r);
}

/**
 * Rebalances the subtree.
 *
 * Precondition:
 *  * root != NULL.
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

            droot->r->v += r->v + r->k2 - r->k1 + 1;

            _itree_set_height(r);

            r = droot->r;
        }

        /* Right-right case. */

        droot->r = r->l;
        r->l = droot;
        *root = r;

        droot->v = _itree_count(droot->r);

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

            l->v = _itree_count(l->r);

            _itree_set_height(l);

            l = droot->l;
        }

        /* Left-left case. */

        droot->l = l->r;
        l->r = droot;
        *root = l;

        l->v += droot->v + droot->k2 - droot->k1 + 1;

        _itree_set_height(droot);
        _itree_set_height(l);
    }
}

int
interval_tree::_itree_descend_l(const uint64_t index,
                                itree_t **root,
                                uint64_t *holes,
                                itree_util_t *util)
{
    itree_t *droot = *root;

    *holes += droot->v + droot->k2 - droot->k1 + 1;

    if (droot->k1 == index + 1) {
        if (util->u == NULL) {
            util->u = droot;
        } else {
            util->l = droot;
        }
    }
    int ret = _itree_insert(index, &droot->l, holes, util);
    if (ret != 0) { return ret; }

    /* Remove the lower node. */
    if (util->l != NULL && util->l == droot->l) {
        const int in_left_subtree = (index == util->l->k2 + 1);
        droot->l = in_left_subtree ? util->l->l : util->l->r;
    }

    return 0;
}

int
interval_tree::_itree_descend_r(const uint64_t index,
                                itree_t **root,
                                uint64_t *holes,
                                itree_util_t *util)
{
    itree_t *droot = *root;

    if (droot->k2 == index - 1) {
        if (util->u == NULL) {
            util->u = droot;
        } else {
            util->l = droot;
        }
    }

    const int below_merge = (util->u != NULL);

    /* Index was added as a new descendant node. */
    if (util->u == NULL && util->u != droot) {
        droot->v++;
    }

    int ret = _itree_insert(index, &droot->r, holes, util);
    if (ret != 0) { return ret; }

    /* Remove the lower node. */
    if (util->l != NULL && util->l == droot->r) {
        const int in_left_subtree = (index == util->l->k2 + 1);
        droot->r = in_left_subtree ? util->l->l : util->l->r;
    }

    /* Adjust the subtree sum after a merge. */
    if (util->l != NULL && util->l != droot && below_merge) {
        droot->v -= util->l->k2 - util->l->k1 + 1;
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
                             uint64_t *holes,
                             itree_util_t *util)
{
    itree_t *droot = *root;
    int ret = 0;

    /* Merge two existing nodes. */
    if (droot == NULL && util->l != NULL) {
        _itree_merge_nodes(util->u, util->l);
        return 0;
    }

    /* Add to existing adjacent node. */
    if (droot == NULL && util->u != NULL) {
        _itree_extend_node(index, util->u);
        return 0;
    }

    /* New node. */
    if (droot == NULL) {
        return _itree_new_node(index, root);
    }

    /* Descend into left or right subtree. */
    if (droot->k1 > index) {
        if ((ret = _itree_descend_l(index, root, holes, util)) != 0) {
            return ret;
        }
    } else if (index > droot->k2) {
        if ((ret = _itree_descend_r(index, root, holes, util)) != 0) {
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
    if (root == NULL) {
        return;
    }

    itree_free(root->l);
    itree_free(root->r);

    free(root);
}
