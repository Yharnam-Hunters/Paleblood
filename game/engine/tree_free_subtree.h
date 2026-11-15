// SPDX-License-Identifier: GPL-2.0-or-later
// The subtree release of the engine's ordered containers (tree_container.h): MSVC's
// std::_Tree::_Erase, which the game compiles again for every kind of set and map.
#pragma once

#include <cstdint>

#include "tree_container.h"

namespace engine {

// What every tree node begins with, whatever key and value follow it. The release reads nothing
// else of a node, so it stands for the node of a tree whose contents are not known yet.
struct TreeNodeLinks {
    TreeNodeLinks *left;
    TreeNodeLinks *parent;
    TreeNodeLinks *right;
    uint8_t color;
    uint8_t is_nil;
};

// Frees every node of the subtree under `node` through the tree's allocator. For each node it
// first frees the node's right subtree (the compiled copy calls itself), then reads the node's
// left link, frees the node, and goes on with that left child. A node whose nil flag is set (the
// head) ends the walk, so an empty subtree frees nothing. A node's links are read before it is
// freed, and the tree's allocator is read again for every node. The head, the tree's links and
// its count are left to the caller.
template <typename Node> void tree_free_subtree(Tree<Node> *tree, Node *node)
{
    while (!node->is_nil) {
        tree_free_subtree(tree, node->right);
        Node *left = node->left;
        tree->allocator->free(node);
        node = left;
    }
}

}  // namespace engine
