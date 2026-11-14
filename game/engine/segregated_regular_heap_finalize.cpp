// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's tear-down (Dantelion2 DLSegregatedRegularHeap; layout in
// segregated_regular_heap.h).
//
// engine_segregated_regular_heap_finalize_02489530 (0x02489530) takes a heap and always returns
// true. The counters, the end descriptor, the class lists and the hash table are cleared (the
// allocation count to all ones), the table goes back to the allocator the heap was set up with,
// and the descriptor pool gives its chunks back too and forgets them. The blocks themselves are
// not looked at.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

constexpr uint64_t no_allocation_count = ~uint64_t{0};

}  // namespace

extern "C" bool bb_engine_segregated_regular_heap_finalize_02489530(Heap *heap)
{
    heap->class_mask = 0;
    heap->capacity = 0;
    heap->available = 0;
    heap->allocation_count = no_allocation_count;
    heap->end.address = 0;
    heap->end.prev = nullptr;
    heap->end.next = nullptr;
    for (ListNode &sentinel : heap->classes) {
        sentinel.prev = nullptr;
        sentinel.next = nullptr;
    }
    for (uint64_t bucket = 0; bucket < hash_buckets; ++bucket)
        heap->hash_table[bucket] = nullptr;

    engine::fixed_allocator::Allocator *pool = &heap->descriptor_pool;
    pool->backing->vtable->free(pool->backing, heap->hash_table);
    if (pool->chunk_tree)
        engine::fixed_allocator::release_chunks(pool, pool->chunk_tree);
    engine::fixed_allocator::forget_chunks(pool);
    return true;
}
