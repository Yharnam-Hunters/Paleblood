// SPDX-License-Identifier: GPL-2.0-or-later
// The input system's maps keyed by a signed 32-bit number: MSVC's std::map / std::multimap red-black
// tree, as engine/msvc_tree.h describes it, but with the key and value packed at 4-byte alignment,
// so the key sits at +0x1c and the value at +0x20 (engine::TreeNode's keys are 8-byte aligned).
// Keys compare signed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace input {

template <typename Value> struct IntMapNode {
    IntMapNode *left;
    IntMapNode *parent;
    IntMapNode *right;
    uint8_t color;
    uint8_t is_nil;
    uint8_t unknown_0x1a[0x2];
    int32_t key;
    Value value;
};

// The map object: its head node (head->parent is the root; the head is the only nil node).
template <typename Value> struct IntMap {
    uint8_t unknown_0x00[0x8];
    IntMapNode<Value> *head;
};

// The first node whose key is not less than `key`, or the head (std::map::lower_bound).
template <typename Value> IntMapNode<Value> *int_map_lower_bound(IntMapNode<Value> *head, int32_t key)
{
    IntMapNode<Value> *found = head;
    for (IntMapNode<Value> *node = head->parent; !node->is_nil;) {
        if (node->key < key) {
            node = node->right;
        } else {
            found = node;
            node = node->left;
        }
    }
    return found;
}

// The first node whose key is greater than `key`, or the head (std::map::upper_bound).
template <typename Value> IntMapNode<Value> *int_map_upper_bound(IntMapNode<Value> *head, int32_t key)
{
    IntMapNode<Value> *found = head;
    for (IntMapNode<Value> *node = head->parent; !node->is_nil;) {
        if (key < node->key) {
            found = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    return found;
}

// The node whose key equals `key`, or the head when there is none (std::map::find).
template <typename Value> IntMapNode<Value> *int_map_find(IntMapNode<Value> *head, int32_t key)
{
    IntMapNode<Value> *found = int_map_lower_bound(head, key);
    return found != head && !(key < found->key) ? found : head;
}

// The node after `node` in key order (the head after the last one). The head itself stays where
// it is, as the game's iterator leaves it.
template <typename Value> IntMapNode<Value> *int_map_next(IntMapNode<Value> *node)
{
    if (node->is_nil)
        return node;
    if (!node->right->is_nil) {
        node = node->right;
        while (!node->left->is_nil)
            node = node->left;
        return node;
    }
    IntMapNode<Value> *parent = node->parent;
    while (!parent->is_nil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

}  // namespace input
