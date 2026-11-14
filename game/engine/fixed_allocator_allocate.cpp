// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size allocator's allocation (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h).
//
// engine_fixed_allocator_allocate_02484c70 (0x02484c70) takes an allocator and returns one free
// element, or null when a new chunk was needed and the backing allocator had none. The heaps take
// their block descriptors from it (0.9M calls in a boot to character creation). The element comes
// from the front of the current chunk's free elements, or else from the first chunk with room,
// which becomes current; a chunk left with no free element leaves the list and stops being
// current. With no chunk left with room, a new chunk comes from the backing allocator: its first
// element is the one returned, the rest are chained in address order, and the chunk joins the end
// of the list (as the current chunk) and the address tree.
#include <cstdint>

#include "fixed_allocator.h"

namespace {

using namespace engine::fixed_allocator;

uintptr_t address_of(const void *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

void *&next_free(void *element)
{
    return *static_cast<void **>(element);
}

Chunk *list_sentinel(Allocator *allocator)
{
    return reinterpret_cast<Chunk *>(&allocator->chunks_with_room);
}

// Takes the first free element of a chunk that has one.
void *take_element(Allocator *allocator, Chunk *chunk)
{
    void *element = chunk->free_elements;
    chunk->free_elements = next_free(element);
    const uint64_t remaining = chunk->free_count - 1;
    allocator->free_count -= 1;
    if (remaining == 0) {
        chunk->next->prev = chunk->prev;
        chunk->prev->next = chunk->next;
        allocator->current = nullptr;
    }
    chunk->free_count = remaining;
    return element;
}

// Links a new chunk's node into the address tree and lets the shared code rebalance it.
void insert_into_tree(Allocator *allocator, TreeNode *node)
{
    TreeNode *parent = nullptr;
    TreeNode **link = &allocator->chunk_tree;
    while (*link) {
        parent = *link;
        link = address_of(node) < address_of(parent) ? &parent->left : &parent->right;
    }
    *link = node;
    tree_insert_fixup(&allocator->chunk_tree, node, parent);
}

// Gets a chunk from the backing allocator and returns its first element.
void *allocate_chunk(Allocator *allocator)
{
    const uint64_t element_size = allocator->element_size;
    const uint64_t chunk_size = allocator->chunk_size;
    BackingAllocator *backing = allocator->backing;
    auto *memory = static_cast<uint8_t *>(backing->vtable->allocate_aligned(backing, chunk_size, allocator->chunk_alignment));
    if (!memory)
        return nullptr;

    const uint64_t elements = allocator->elements_per_chunk;
    auto *chunk = reinterpret_cast<Chunk *>(memory + chunk_size - sizeof(Chunk));
    chunk->free_count = elements - 1;
    chunk->free_elements = memory + element_size;
    Chunk *last = allocator->chunks_with_room.prev;
    allocator->chunks_with_room.prev = chunk;
    last->next = chunk;
    chunk->next = list_sentinel(allocator);
    chunk->prev = last;
    allocator->current = chunk;
    allocator->free_count += elements - 1;
    allocator->chunk_count += 1;

    // Elements 1 .. n-1 are free, each pointing at the next. A chunk of fewer than two elements
    // would never end this walk, in the original as here; the game never configures one.
    uint8_t *last_element = memory + (elements - 1) * element_size;
    for (uint8_t *element = memory + element_size; element != last_element; element += element_size)
        next_free(element) = element + element_size;
    next_free(last_element) = nullptr;

    insert_into_tree(allocator, &chunk->node);
    return memory;
}

}  // namespace

extern "C" void *bb_engine_fixed_allocator_allocate_02484c70(Allocator *allocator)
{
    Chunk *chunk = allocator->current;
    if (!chunk) {
        chunk = allocator->chunks_with_room.next;
        if (chunk == list_sentinel(allocator))
            return allocate_chunk(allocator);
        allocator->current = chunk;
    }
    return take_element(allocator, chunk);
}
