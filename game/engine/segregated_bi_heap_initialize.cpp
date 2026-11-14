// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's set-up (Dantelion2 DLSegregatedBiHeap; descriptors and lists
// in segregated_bi_heap.h).
//
// engine_segregated_bi_heap_initialize_02487e50 (0x02487e50) takes a heap, the allocator its
// descriptors' chunks come from and the memory it is to manage, and returns whether the heap is
// ready. The range runs from the memory's start rounded up to 16, for the size less that rounding,
// rounded down to 16; no size is too small. The descriptor allocator is set up empty (0x66
// descriptors to a chunk) and the heap marked set up; the counters, the terminator (at the range's
// limit, linked to itself) and both allocation chains start empty. The whole range then becomes
// one free block. When the descriptor allocator cannot provide its descriptor, the allocator is
// torn down again, the heap marked not set up, and the set-up fails.
#include <cstdint>

#include "segregated_bi_heap.h"

namespace {

using namespace engine::segregated_bi_heap;

constexpr uint64_t descriptors_per_chunk = 0x66;
constexpr uint64_t descriptor_chunk_alignment = 8;
constexpr uint32_t range_alignment = 16;

}  // namespace

extern "C" bool bb_engine_segregated_bi_heap_initialize_02487e50(Heap *heap,
                                                                 engine::fixed_allocator::BackingAllocator *backing,
                                                                 void *memory, uint64_t size)
{
    // The padding up to the alignment is computed from the address's low 32 bits.
    const uint64_t padding = (0u - static_cast<uint32_t>(address_of(memory))) & (range_alignment - 1);
    const uintptr_t start = address_of(memory) + padding;
    const uint64_t bytes = (size - padding) & ~uint64_t{range_alignment - 1};
    heap->lowest = start;
    heap->limit = start + bytes;
    engine::fixed_allocator::set_up(descriptor_pool(heap), backing, sizeof(Descriptor), descriptors_per_chunk,
                                    descriptor_chunk_alignment);
    heap->initialized = true;
    heap->allocation_count = 0;
    heap->available = bytes;

    Descriptor *terminator = &heap->terminator;
    terminator->address = start + bytes;
    terminator->geo_prev = terminator;
    terminator->geo_next = terminator;
    terminator->list_prev = terminator;
    terminator->list_next = terminator;
    heap->top_allocations = nullptr;
    heap->bottom_allocations = nullptr;

    Descriptor *block = pool_allocate(descriptor_pool(heap));
    if (!block) {
        // (The game tests the flag it has just set again: the allocator call could have cleared it.)
        if (heap->initialized)
            tear_down_descriptor_pool(heap);
        return false;
    }
    block->address = start;
    block->geo_next = terminator;
    terminator->geo_prev = block;
    terminator->geo_next = block;
    block->geo_prev = terminator;
    free_list_insert_after(terminator, block);
    return true;
}
