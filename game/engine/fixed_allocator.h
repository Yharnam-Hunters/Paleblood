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
    void *unknown_slots[10];
    void *(*allocate)(BackingAllocator *, uint64_t size);
    void *(*allocate_aligned)(BackingAllocator *, uint64_t size, uint64_t alignment);
    void *unknown_slots_0x60[2];
    void (*free)(BackingAllocator *, void *memory);
};
static_assert(offsetof(BackingAllocatorVtable, allocate) == 0x50);
static_assert(offsetof(BackingAllocatorVtable, allocate_aligned) == 0x58);
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

// The engine's red-black tree code (shared with the heaps): remove a node, and rebalance after
// linking a new node below `parent`.
RT_ORIGINAL(0x024f8460, tree_remove, void(TreeNode **root, TreeNode *node));
RT_ORIGINAL(0x024f8350, tree_insert_fixup, void(TreeNode **root, TreeNode *node, TreeNode *parent));

// Gives every chunk of a subtree back to the backing allocator (fixed_allocator_release_chunks.cpp).
RT_ORIGINAL(0x02484ae0, release_chunks, void(Allocator *, TreeNode *subtree));

// The memory of the chunk whose descriptor holds this tree node.
inline void *chunk_memory(const Allocator *allocator, TreeNode *node)
{
    const uintptr_t descriptor = reinterpret_cast<uintptr_t>(node) - offsetof(Chunk, node);
    return reinterpret_cast<void *>(descriptor + sizeof(Chunk) - allocator->chunk_size);
}

// Sets an allocator up empty: no chunks yet, elements of `element_size`, `elements_per_chunk` to a
// chunk (whose memory ends in its descriptor), chunks aligned to `chunk_alignment` and taken from
// `backing`.
inline void set_up(Allocator *allocator, BackingAllocator *backing, uint64_t element_size,
                   uint64_t elements_per_chunk, uint64_t chunk_alignment)
{
    allocator->last_freed_into = nullptr;
    allocator->current = nullptr;
    allocator->chunk_alignment = chunk_alignment;
    allocator->chunk_count = 0;
    allocator->free_count = 0;
    allocator->element_size = element_size;
    allocator->elements_per_chunk = elements_per_chunk;
    allocator->chunk_size = elements_per_chunk * element_size + sizeof(Chunk);
    allocator->backing = backing;
    auto *sentinel = reinterpret_cast<Chunk *>(&allocator->chunks_with_room);
    allocator->chunks_with_room.prev = sentinel;
    allocator->chunks_with_room.next = sentinel;
    allocator->chunk_tree = nullptr;
}

// Forgets every chunk (after releasing them): no current chunk, an empty list and tree.
inline void forget_chunks(Allocator *allocator)
{
    allocator->last_freed_into = nullptr;
    allocator->current = nullptr;
    allocator->chunk_tree = nullptr;
    allocator->chunks_with_room.prev = nullptr;
    allocator->chunks_with_room.next = nullptr;
}

}  // namespace engine::fixed_allocator
