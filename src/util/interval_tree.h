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

namespace kpq {

/**
 * A specialized interval tree implementation which stores the number of elements
 * within the left subtree in each node. The value domain is [0, inf[,
 * i.e. nonnegative.
 *
 * In our case, values within the tree are element indices which are known taken.
 * All other indices may be either taken or not.
 */
class interval_tree
{
private:
    typedef struct __itree_t {
        struct __itree_t *l, *r;    /**< The left and right child nodes. */
        uint64_t k1, k2;            /**< The key interval [k1, k2]. */
        uint64_t v;                 /**< The # of elements in the left subtree. */
        uint8_t h;                  /**< The height of this node. height(node without
                                     *   children) == 0. */
        bool in_use;
    } itree_t;

    typedef struct {
        itree_t *u, *l;             /**< Upper and lower adjacent nodes. */
    } itree_util_t;

public:
    /** Adds the given index to the set. */
    void insert(const uint64_t index);

    /** Returns the number of untaken items before the given index. */
    uint64_t num_untaken_before(const uint64_t index) const;

    /** Returns the n'th untaken index. For example, if n = 1, returns an index
     * i such that i itself is untaken (or at least not known taken) and there are
     * i other untaken indices before it. Complexity is log(# nodes in tree). */
    uint64_t nth_untaken_ix(const uint64_t n) const;

    /** Clears the set. */
    void clear();

    /** Copies the given tree by copy-on-write, i.e. initially copy
     * only the pointer, and the full tree only on writes. */
    interval_tree &operator=(const interval_tree &that);

public:
    struct reuse {
        bool operator()(const itree_t &t) const {
            return !t.in_use;
        }
    };

private:
    int _nth_untaken_ix(const uint64_t n,
                        const itree_t *node,
                        const uint64_t taken_to_left_in_supertree) const;

    itree_t *copy(const itree_t *that);

private: /* Raw itree methods. */
    int itree_insert(const uint64_t index,
                     itree_t **root);
    void itree_free(itree_t *root);

    int
    _itree_insert(const uint64_t index,
                  itree_t **root,
                  itree_util_t *util);
    int
    _itree_new_node(const uint64_t index,
                    itree_t **root);
    void
    _itree_extend_node(const uint64_t index,
                       itree_t *node);
    void
    _itree_merge_nodes(itree_t *upper,
                       itree_t *lower);
    void
    _itree_rebalance(itree_t **root);
    inline int8_t
    _itree_height(const itree_t *node) const;
    inline void
    _itree_set_height(itree_t *node);
    inline uint64_t
    _itree_count(const itree_t *root) const;
    int
    _itree_descend_l(const uint64_t index,
                     itree_t **root,
                     itree_util_t *util);
    int
    _itree_descend_r(const uint64_t index,
                     itree_t **root,
                     itree_util_t *util);

private:
    itree_t *m_root;

    item_allocator<itree_t, reuse, 32> m_allocator;
};

#include "interval_tree_inl.h"

} // namespace kpq

#endif /*  __INTERVAL_TREE_H */
