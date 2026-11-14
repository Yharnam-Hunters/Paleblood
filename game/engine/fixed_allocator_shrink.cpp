// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size allocator's shrink (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h).
//
// engine_fixed_allocator_shrink_02484f20 (0x02484f20) takes an allocator and gives back to the
// backing allocator every chunk whose elements are all free. Only the chunks in the list of chunks
// with room are looked at (a chunk with every element free is always there). A released chunk
// stops being the current chunk or the last one freed into, leaves the list and the address tree,
// and takes its elements out of the free count.
#include <cstdint>

#include "fixed_allocator.h"

extern "C" void bb_engine_fixed_allocator_shrink_02484f20(engine::fixed_allocator::Allocator *allocator)
{
    using namespace engine::fixed_allocator;
    auto *sentinel = reinterpret_cast<Chunk *>(&allocator->chunks_with_room);
    const uint64_t per_chunk = allocator->elements_per_chunk;
    for (Chunk *chunk = allocator->chunks_with_room.next, *next; chunk != sentinel; chunk = next) {
        next = chunk->next;
        if (chunk->free_count != per_chunk)
            continue;
        if (allocator->last_freed_into == chunk)
            allocator->last_freed_into = nullptr;
        if (allocator->current == chunk)
            allocator->current = nullptr;
        allocator->chunk_count -= 1;
        allocator->free_count -= per_chunk;
        chunk->next->prev = chunk->prev;
        chunk->prev->next = chunk->next;
        tree_remove(&allocator->chunk_tree, &chunk->node);
        allocator->backing->vtable->free(allocator->backing, chunk_memory(allocator, &chunk->node));
    }
}
