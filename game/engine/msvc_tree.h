// SPDX-License-Identifier: GPL-2.0-or-later
// The game's ordered maps: MSVC's std::map red-black tree, as laid out in memory. The map holds
// a head node; head->parent is the root, head->left the first (smallest) node, and every leaf
// link points back to the head, which is the only node marked nil.
#pragma once

#include <cstdint>

namespace engine {

template <typename Key, typename Value> struct TreeNode {
    TreeNode *left;
    TreeNode *parent;
    TreeNode *right;
    uint8_t color;
    uint8_t is_nil;
    alignas(8) Key key;
    Value value;
};

// The node after `node` in key order (the head after the last one), walked the way the game's
// own iterators do.
template <typename Key, typename Value> TreeNode<Key, Value> *tree_next(TreeNode<Key, Value> *node)
{
    TreeNode<Key, Value> *right = node->right;
    if (!right->is_nil) {
        // the leftmost node of the right subtree
        TreeNode<Key, Value> *next;
        do {
            next = right;
            right = right->left;
        } while (!right->is_nil);
        return next;
    }
    // up while coming from a right child
    for (;;) {
        TreeNode<Key, Value> *parent = node->parent;
        if (parent->is_nil) return parent;
        const bool from_right = node == parent->right;
        node = parent;
        if (!from_right) return parent;
    }
}

// The node whose key equals `key`, or the head when there is none (std::map::find).
template <typename Key, typename Value> TreeNode<Key, Value> *tree_find(TreeNode<Key, Value> *head, Key key)
{
    TreeNode<Key, Value> *found = head;
    for (TreeNode<Key, Value> *node = head->parent; !node->is_nil;) {
        if (node->key < key) {
            node = node->right;
        } else {
            found = node;
            node = node->left;
        }
    }
    return found != head && !(key < found->key) ? found : head;
}

}  // namespace engine
