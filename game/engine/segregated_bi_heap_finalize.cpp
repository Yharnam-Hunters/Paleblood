// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's tear-down (Dantelion2 DLSegregatedBiHeap; descriptors and lists
// in segregated_bi_heap.h).
//
// engine_segregated_bi_heap_finalize_02488650 (0x02488650) takes a heap and always returns true.
// A heap that is set up gives its descriptor allocator's chunks back (the descriptors with them)
// and is marked not set up. The terminator, both allocation chains and the range are cleared; the
// allocation count and the free bytes are left as they are, and the managed memory is not looked
// at.
#include "segregated_bi_heap.h"

extern "C" bool bb_engine_segregated_bi_heap_finalize_02488650(engine::segregated_bi_heap::Heap *heap)
{
    using namespace engine::segregated_bi_heap;
    if (heap->initialized)
        tear_down_descriptor_pool(heap);
    heap->limit = 0;
    heap->lowest = 0;
    heap->top_allocations = nullptr;
    heap->bottom_allocations = nullptr;
    heap->terminator = Descriptor{};
    return true;
}
