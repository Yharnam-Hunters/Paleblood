// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the subtree release of a set of 64-bit keys (tree_container.h), a template instance
// (MSVC's std::_Tree::_Erase).
//
// engine_set_free_subtree_02b3bda0 (0x02b3bda0) frees every node of the subtree under `node`
// through the set's allocator. For each node it first frees the node's right subtree (by calling
// itself), then reads the node's left link, frees the node, and goes on with that left child. A
// node whose nil flag is set (the head) ends the walk, so an empty subtree frees nothing. A
// node's links are read before it is freed, and the set's allocator is read again for every node.
//
// The head, the set's links and its count are left to the caller. The one caller (0x02b3b330)
// fills a set on its stack, frees the whole tree through here from the root, then points the head
// at itself, clears the count and frees the head.
#include <cstddef>
#include <cstdint>

#include "tree_container.h"

namespace {

using Node = engine::TreeSetNode<uint64_t>;
using Set = engine::Tree<Node>;

// The node size the set's constructor (0x02b3bc80) asks the allocator for, and where the set
// keeps that allocator.
static_assert(sizeof(Node) == 0x28 && offsetof(Set, allocator) == 0x18);

}  // namespace

extern "C" void bb_engine_set_free_subtree_02b3bda0(Set *set, Node *node)
{
    while (!node->is_nil) {
        bb_engine_set_free_subtree_02b3bda0(set, node->right);
        Node *left = node->left;
        set->allocator->free(node);
        node = left;
    }
}
