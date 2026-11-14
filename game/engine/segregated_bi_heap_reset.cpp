// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: clearing the segregated bi-heap's state (Dantelion2 DLSegregatedBiHeap; layout in
// segregated_bi_heap.h).
//
// engine_segregated_bi_heap_reset_02487dd0 (0x02487dd0) clears everything from the set-up flag to
// the end of the state: not set up, an all-zero terminator, empty allocation chains, no range and
// zero counters. The descriptor allocator's bytes before the flag are left alone.
#include <cstdint>

#include "segregated_bi_heap.h"

using namespace engine::segregated_bi_heap;

extern "C" void bb_engine_segregated_bi_heap_reset_02487dd0(Heap *heap)
{
    heap->initialized = false;
    heap->terminator = Descriptor{};
    heap->bottom_allocations = nullptr;
    heap->top_allocations = nullptr;
    heap->lowest = 0;
    heap->limit = 0;
    heap->allocation_count = 0;
    heap->available = 0;
}
