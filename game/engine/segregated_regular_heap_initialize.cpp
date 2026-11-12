// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's set-up (Dantelion2 DLSegregatedRegularHeap; layout in
// segregated_regular_heap.h).
//
// engine_segregated_regular_heap_initialize_02489340 (0x02489340) takes a heap, the allocator it
// takes its own tables from, and the memory it is to manage. The hash table of blocks in use comes
// from that allocator first (false when it has none); a range of less than 16 bytes between the
// memory's start rounded up to 16 and its end rounded down is refused too. Then the descriptor
// pool is set up empty (elements of a descriptor's size, 0x66 to a chunk) on the same allocator,
// the lists and the table are emptied, and the whole range becomes one free block: its descriptor
// (from the pool, not checked) forms the address ring with the heap's end descriptor and goes into
// its class list.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

constexpr uint64_t descriptors_per_chunk = 0x66;
constexpr uint64_t descriptor_chunk_alignment = 8;
constexpr uintptr_t range_alignment_mask = 0xf;
constexpr uint64_t smallest_range = 0x10;

}  // namespace

extern "C" bool bb_engine_segregated_regular_heap_initialize_02489340(Heap *heap,
                                                                     engine::fixed_allocator::BackingAllocator *backing,
                                                                     void *memory, uint64_t size)
{
    heap->hash_table = static_cast<Descriptor **>(
        backing->vtable->allocate(backing, hash_buckets * sizeof(Descriptor *)));
    if (!heap->hash_table)
        return false;
    const uintptr_t start = (reinterpret_cast<uintptr_t>(memory) + range_alignment_mask) & ~range_alignment_mask;
    const uintptr_t end = (reinterpret_cast<uintptr_t>(memory) + size) & ~range_alignment_mask;
    const uint64_t bytes = end - start;
    if (bytes < smallest_range)
        return false;

    heap->range_start = start;
    heap->range_end = end;
    heap->available = bytes;
    heap->capacity = bytes;
    heap->allocation_count = 0;
    engine::fixed_allocator::set_up(&heap->descriptor_pool, backing, sizeof(Descriptor), descriptors_per_chunk,
                                    descriptor_chunk_alignment);
    heap->class_mask = 0;
    for (ListNode &sentinel : heap->classes) {
        sentinel.prev = &sentinel;
        sentinel.next = &sentinel;
    }
    for (uint64_t bucket = 0; bucket < hash_buckets; ++bucket)
        heap->hash_table[bucket] = nullptr;
    heap->end.node.prev = nullptr;
    heap->end.node.next = nullptr;
    heap->end.address = end;

    Descriptor *block = descriptor_allocate(&heap->descriptor_pool);
    heap->end.next = block;
    block->prev = &heap->end;
    block->next = &heap->end;
    heap->end.prev = block;
    block->address = start;
    if (list_push_front(heap, size_class(bytes), block))
        heap->class_mask |= class_bit(size_class(bytes));
    return true;
}
