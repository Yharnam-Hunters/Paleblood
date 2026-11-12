// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's free of a top allocation (Dantelion2 DLSegregatedBiHeap's
// BackFree; descriptors and lists in segregated_bi_heap.h).
//
// engine_segregated_bi_heap_free_top_02488f40 (0x02488f40) takes a heap and an address (null does
// nothing) and frees an allocation made from the top of the heap. It is the heap's free
// (segregated_bi_heap_free.cpp) with only the top allocations searched: an address outside the
// heap's range is reported and looked up anyway; one that is no top allocation is reported (after
// the integrity check's report when that fails) and nothing changes. A found block is freed as the
// free does it: out of its chain, the counters updated, merged with the free blocks around it and
// linked into the free list. The error reports return, and the code goes on after them, as the
// game does.
#include <cstdint>

#include "segregated_bi_heap.h"

namespace {

using namespace engine::segregated_bi_heap;

constexpr int32_t line_outside_heap = 0x43c;
constexpr int32_t line_contradiction = 0x45d;
// "Requested block not marked as allocated. Check and see if you called BackFree() twice, or
// called against Alloc()ed block."
RT_GLOBAL(0x04d3ca6a, message_not_allocated, const char);
constexpr int32_t line_not_allocated = 0x467;

}  // namespace

extern "C" void bb_engine_segregated_bi_heap_free_top_02488f40(Heap *heap, void *pointer)
{
    const uintptr_t address = address_of(pointer);
    if (!address)
        return;
    if (address < heap->lowest || address >= heap->limit)
        report(line_outside_heap, message_outside_heap.address());

    Descriptor *previous;
    Descriptor *block = find_allocation(heap->top_allocations, address, &previous);
    if (!block) {
        if (!heap_is_consistent(heap))
            report(line_contradiction, message_contradiction.address());
        report(line_not_allocated, message_not_allocated.address());
        return;
    }
    release_allocation(heap, &heap->top_allocations, block, previous);
}
