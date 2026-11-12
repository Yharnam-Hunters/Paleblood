// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's robust heap (Dantelion2 DLRobustHeap): the block layout, its free lists and the
// size-ordered tree of large free blocks.
//
// The heap's memory is a chain of blocks. A block starts with two words: a pointer to the block
// before it, and the address of the next block with two flags in the low bits (this block in use;
// the block before it in use). Only a free block's successor needs the back pointer, so an
// allocation's payload, which starts after the two words, also covers the next block's first word:
// a block's usable size is (next - block) - 8.
//
// Free blocks with a usable size up to 0x1f8 sit in 31 circular lists with sentinels, one per
// size (0x18, 0x28, ... 0x1f8). Larger free blocks are nodes of a red-black tree ordered by size
// (equal sizes go right), its colour in bit 0 of the parent word. The tree's insertion fix-up and
// removal are the engine's own functions, shared with other trees.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace engine::robust_heap {

// The payload of a free block: list links for a small block, tree links for a large one.
struct FreeNode {
    FreeNode *next_or_left;     // small: next in the list; large: left child
    FreeNode *prev_or_right;    // small: previous in the list; large: right child
    uintptr_t parent_and_colour;
    uint64_t size;              // large only: the usable size
};
static_assert(offsetof(FreeNode, parent_and_colour) == 0x10 && offsetof(FreeNode, size) == 0x18);

struct Block {
    Block *prev;                // valid only while the block before this one is free
    uintptr_t next_and_flags;
    FreeNode payload;           // the allocation starts here
};
static_assert(offsetof(Block, next_and_flags) == 0x8 && offsetof(Block, payload) == 0x10);

constexpr uintptr_t block_in_use = 0x1;
constexpr uintptr_t prev_in_use = 0x2;
constexpr uintptr_t tree_colour = 0x1;

constexpr uint64_t block_header_size = offsetof(Block, payload);
constexpr uint64_t block_overhead = 8;          // what a block costs beyond its usable size
constexpr uint64_t smallest_usable = 0x18;
constexpr uint64_t largest_small = 0x1f8;
constexpr uint64_t size_step = 0x10;
// Usable sizes are 8 more than a multiple of 16: (n - 9) rounded down to 16 is the step below the
// smallest such size that holds n (0x18 holds n = 0x18, 0x28 holds n = 0x19).
constexpr uint64_t size_class_bias = 9;
constexpr uint64_t small_bin_count = 31;
constexpr uint64_t small_bin_stride = 0x10;     // a sentinel is only the two list links
// A free large block bigger than the request by more than this is split and its rest stays where
// it was in the tree; otherwise the block leaves the tree first.
constexpr uint64_t split_in_place_margin = 0x200;

// The heap's state (part of the DLRobustHeap object).
struct Heap {
    uint8_t unknown_0x00[0x8];
    uint64_t available;         // free bytes, block overhead included
    uint64_t allocation_count;
    uint8_t *small_bins;        // 31 list sentinels, 16 bytes apart
    FreeNode *large_root;
};
static_assert(offsetof(Heap, available) == 0x8 && offsetof(Heap, allocation_count) == 0x10);
static_assert(offsetof(Heap, small_bins) == 0x18 && offsetof(Heap, large_root) == 0x20);

// Red-black tree helpers (shared engine code): remove a node, and rebalance after linking a new
// node below `parent`.
RT_ORIGINAL(0x024f8460, tree_remove, void(FreeNode **root, FreeNode *node));
RT_ORIGINAL(0x024f8350, tree_insert_fixup, void(FreeNode **root, FreeNode *node, FreeNode *parent));

inline Block *block_of(FreeNode *payload)
{
    return reinterpret_cast<Block *>(reinterpret_cast<uintptr_t>(payload) - block_header_size);
}

// A free block's successor. Free neighbours are always merged, so a free block follows a block in
// use and its flags are exactly prev_in_use; the heap subtracts that flag rather than masking.
inline Block *free_block_next(const Block *block)
{
    return reinterpret_cast<Block *>(block->next_and_flags - prev_in_use);
}

inline uint64_t free_block_usable(const Block *block)
{
    return reinterpret_cast<uintptr_t>(free_block_next(block)) - reinterpret_cast<uintptr_t>(block) - block_overhead;
}

// The usable size a request gets: at least 0x18, otherwise rounded up to 8 more than a multiple
// of 16.
inline uint64_t usable_for_request(uint64_t request)
{
    if (request < smallest_usable)
        return smallest_usable;
    return ((request - size_class_bias) & ~(size_step - 1)) + smallest_usable;
}

// The sentinel of small list `index` (the list for usable size 0x18 + 16 * index). Sentinels hold
// only the two list links, so they are 16 bytes apart, not a FreeNode apart.
inline FreeNode *small_bin_at(Heap *heap, uint64_t index)
{
    return reinterpret_cast<FreeNode *>(heap->small_bins + index * small_bin_stride);
}

inline uint64_t small_bin_index(uint64_t usable)
{
    return (usable - size_class_bias) / size_step;
}

inline FreeNode *small_bin(Heap *heap, uint64_t usable)
{
    return small_bin_at(heap, small_bin_index(usable));
}

inline bool list_empty(const FreeNode *sentinel)
{
    return sentinel->prev_or_right == sentinel;
}

inline void list_unlink(FreeNode *node)
{
    FreeNode *next = node->next_or_left;
    FreeNode *prev = node->prev_or_right;
    prev->next_or_left = next;
    next->prev_or_right = prev;
}

// Appends a node at the list's end (the sentinel's previous).
inline void list_push_back(FreeNode *sentinel, FreeNode *node)
{
    FreeNode *last = sentinel->prev_or_right;
    sentinel->prev_or_right = node;
    last->next_or_left = node;
    node->prev_or_right = last;
    node->next_or_left = sentinel;
}

// Links a large free block into the tree: down to a leaf (equal sizes to the right), then the
// shared fix-up, which also sets the node's own links.
inline void tree_insert(Heap *heap, FreeNode *node, uint64_t usable)
{
    node->size = usable;
    FreeNode **link = &heap->large_root;
    FreeNode *parent = nullptr;
    for (FreeNode *at = *link; at; at = *link) {
        link = at->size <= usable ? &at->prev_or_right : &at->next_or_left;
        parent = at;
    }
    *link = node;
    tree_insert_fixup(&heap->large_root, node, parent);
}

// Puts the rest of a split block where it belongs by its usable size: nowhere below 0x18 (a
// fragment that waits to be merged), a small list up to 0x1f8, else the tree.
inline void file_free_block(Heap *heap, Block *block, uint64_t usable)
{
    if (usable > largest_small)
        tree_insert(heap, &block->payload, usable);
    else if (usable >= smallest_usable)
        list_push_back(small_bin(heap, usable), &block->payload);
}

// Marks a free block in use, and its successor as following a block in use.
inline void mark_in_use(Block *block)
{
    free_block_next(block)->next_and_flags += prev_in_use;
    block->next_and_flags += block_in_use;
}

// Splits a free block (which follows a block in use): the front `usable` bytes become an
// allocation, the rest a free block whose back pointer the next block gets. Returns the rest.
inline Block *split_front(Block *block, uint64_t usable)
{
    const uintptr_t next_and_flags = block->next_and_flags;
    Block *rest = reinterpret_cast<Block *>(reinterpret_cast<uintptr_t>(&block->payload) + usable - block_overhead);
    free_block_next(block)->prev = rest;
    block->next_and_flags = reinterpret_cast<uintptr_t>(rest) | prev_in_use | block_in_use;
    rest->next_and_flags = next_and_flags;
    return rest;
}

// The tree's smallest node.
inline FreeNode *tree_leftmost(FreeNode *node)
{
    while (node->next_or_left)
        node = node->next_or_left;
    return node;
}

// The next node in size order, through the parent links.
inline FreeNode *tree_successor(FreeNode *node)
{
    if (node->prev_or_right)
        return tree_leftmost(node->prev_or_right);
    for (;;) {
        FreeNode *parent = reinterpret_cast<FreeNode *>(node->parent_and_colour & ~tree_colour);
        if (!parent || node != parent->prev_or_right)
            return parent;
        node = parent;
    }
}

}  // namespace engine::robust_heap
