// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's ordered containers: MSVC's std::set and std::map trees (msvc_tree.h) that carry
// their allocator, as the arrays do (container.h).
#pragma once

#include <cstddef>
#include <cstdint>

#include "allocator.h"

namespace engine {

// A set's node: the map's node (msvc_tree.h) without a value.
template <typename Key> struct TreeSetNode {
    TreeSetNode *left;
    TreeSetNode *parent;
    TreeSetNode *right;
    uint8_t color;
    uint8_t is_nil;
    alignas(8) Key key;
};

// The container itself: its head node, its element count and the allocator its nodes come from.
// The constructor of the set of 64-bit keys (0x02b3bc80) checks the heap it is given, clears the
// count, stores the allocator and allocates the head.
template <typename Node> struct Tree {
    uint64_t unknown_0x00;
    Node *head;
    uint64_t size;
    Allocator *allocator;
};

}  // namespace engine
