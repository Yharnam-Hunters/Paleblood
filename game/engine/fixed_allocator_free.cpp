// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size allocator's free (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h).
//
// engine_fixed_allocator_free_02484de0 (0x02484de0) takes an allocator and an element (null does
// nothing); the heaps free their block descriptors through it (0.8M calls in a boot to character
// creation). The element's chunk is the one the last free went to when the element lies within it,
// else the chunk the address tree finds, which becomes that last chunk; an element of no chunk is
// reported (the report returns, and the code goes on with no chunk, as the game does). The
// element goes to the front of its chunk's free elements, and a chunk that had none joins the end
// of the list of chunks with room. A chunk whose elements are now all free goes back to the
// backing allocator instead, when at least two chunks' worth of elements would otherwise be free.
#include <cstdint>

#include "engine.h"
#include "fixed_allocator.h"

namespace {

using namespace engine::fixed_allocator;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLFixedAllocator.cpp"
RT_GLOBAL(0x04d3c555, source_file, const char);
// "failed to find chunk to deallocate."
RT_GLOBAL(0x04d3c609, message_no_chunk, const char);
constexpr int32_t line_no_chunk = 0x132;
// An emptied chunk is released only while this many chunks' worth of elements are free.
constexpr uint64_t chunks_kept_free = 2;

uintptr_t address_of(const void *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

Chunk *chunk_of(TreeNode *node)
{
    return reinterpret_cast<Chunk *>(address_of(node) - offsetof(Chunk, node));
}

// A chunk's descriptor lies at most chunk_size bytes after any of its elements (the test the
// allocator uses, for chunk pointers and tree nodes alike).
bool within_chunk(const Allocator *allocator, uintptr_t descriptor, uintptr_t element)
{
    return descriptor >= element && descriptor <= element + allocator->chunk_size;
}

// The chunk holding an element: the last chunk freed into, else a search of the address tree.
Chunk *find_chunk(Allocator *allocator, void *element)
{
    Chunk *last = allocator->last_freed_into;
    if (within_chunk(allocator, address_of(last), address_of(element)))
        return last;
    Chunk *found = nullptr;
    for (TreeNode *node = allocator->chunk_tree; node;) {
        if (address_of(node) < address_of(element)) {
            node = node->right;
        } else if (address_of(node) > address_of(element) + allocator->chunk_size) {
            node = node->left;
        } else {
            found = chunk_of(node);
            break;
        }
    }
    if (!found)
        engine::fatal_error(source_file.address(), line_no_chunk, message_no_chunk.address());
    allocator->last_freed_into = found;
    return found;
}

// Gives a chunk whose elements are all free back to the backing allocator.
void release_chunk(Allocator *allocator, Chunk *chunk, uint64_t free_count, uint64_t chunk_free)
{
    if (allocator->current == chunk)
        allocator->current = nullptr;
    allocator->last_freed_into = nullptr;
    allocator->chunk_count -= 1;
    allocator->free_count = free_count - chunk_free;
    chunk->next->prev = chunk->prev;
    chunk->prev->next = chunk->next;
    tree_remove(&allocator->chunk_tree, &chunk->node);
    void *memory = reinterpret_cast<void *>(address_of(chunk) + sizeof(Chunk) - allocator->chunk_size);
    allocator->backing->vtable->free(allocator->backing, memory);
}

}  // namespace

extern "C" void bb_engine_fixed_allocator_free_02484de0(Allocator *allocator, void *element)
{
    if (!element)
        return;
    Chunk *chunk = find_chunk(allocator, element);
    const uint64_t free_count = allocator->free_count + 1;
    allocator->free_count = free_count;
    const uint64_t chunk_free = chunk->free_count + 1;
    if (chunk->free_count == 0) {
        // The chunk had no room: it joins the end of the list.
        Chunk *last = allocator->chunks_with_room.prev;
        allocator->chunks_with_room.prev = chunk;
        last->next = chunk;
        chunk->next = reinterpret_cast<Chunk *>(&allocator->chunks_with_room);
        chunk->prev = last;
    } else if (chunk_free == allocator->elements_per_chunk && free_count >= chunks_kept_free * allocator->elements_per_chunk) {
        release_chunk(allocator, chunk, free_count, chunk_free);
        return;
    }
    chunk->free_count = chunk_free;
    *static_cast<void **>(element) = chunk->free_elements;
    chunk->free_elements = element;
}
