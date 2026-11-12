// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's fixed-size allocator (Dantelion2 DLFixedAllocator): equal elements carved from
// chunks that a backing allocator provides.
//
// Each chunk ends in its own descriptor: links in the list of chunks that have free elements, a
// node in a red-black tree of all chunks ordered by address (bit 0 of the parent word = black, the
// engine's shared tree code), the chunk's list of free elements (each free element's first word
// points at the next) and how many of its elements are free. A chunk is in the list exactly while
// it has a free element.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace engine::fixed_allocator {

struct TreeNode {
    TreeNode *left;
    TreeNode *right;
    uintptr_t parent_and_colour;
};

struct Chunk {                  // at the end of the chunk's memory
    Chunk *next;                // the list of chunks with free elements
    Chunk *prev;
    TreeNode node;              // the address tree
    void *free_elements;
    uint64_t free_count;
};
static_assert(offsetof(Chunk, node) == 0x10 && offsetof(Chunk, free_elements) == 0x28);
static_assert(offsetof(Chunk, free_count) == 0x30 && sizeof(Chunk) == 0x38);

struct BackingAllocator;
struct BackingAllocatorVtable {
    void *unknown_slots[14];
    void (*free)(BackingAllocator *, void *memory);
};
static_assert(offsetof(BackingAllocatorVtable, free) == 0x70);

struct BackingAllocator {
    const BackingAllocatorVtable *vtable;
};

// A chunk sentinel: only the two list links.
struct ChunkLinks {
    Chunk *next;
    Chunk *prev;
};

struct Allocator {
    Chunk *current;             // the chunk allocations take from
    Chunk *last_freed_into;     // the chunk the last free went to, tried first
    uint64_t element_size;
    uint64_t chunk_size;        // the chunk's memory, its descriptor included
    uint64_t chunk_alignment;
    uint64_t elements_per_chunk;
    uint64_t free_count;        // free elements in all chunks
    uint64_t chunk_count;
    BackingAllocator *backing;
    ChunkLinks chunks_with_room;
    TreeNode *chunk_tree;
};
static_assert(offsetof(Allocator, last_freed_into) == 0x8 && offsetof(Allocator, chunk_size) == 0x18);
static_assert(offsetof(Allocator, elements_per_chunk) == 0x28 && offsetof(Allocator, free_count) == 0x30);
static_assert(offsetof(Allocator, chunk_count) == 0x38 && offsetof(Allocator, backing) == 0x40);
static_assert(offsetof(Allocator, chunks_with_room) == 0x48 && offsetof(Allocator, chunk_tree) == 0x58);

// The engine's red-black tree removal (shared with the heaps).
RT_ORIGINAL(0x024f8460, tree_remove, void(TreeNode **root, TreeNode *node));

}  // namespace engine::fixed_allocator
