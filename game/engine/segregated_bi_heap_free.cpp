// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's free (Dantelion2 DLSegregatedBiHeap; descriptors and lists in
// segregated_bi_heap.h).
//
// engine_segregated_bi_heap_free_02488900 (0x02488900) takes a heap and an address (null does
// nothing); it is the heap's free, reached from the engine's allocator front ends (2.7M calls in
// a boot to character creation). An address outside the heap's range is reported and looked up
// anyway. The block is looked for in the bottom allocations, then in the top ones; one in neither
// is reported (after the heap's integrity check, when that fails) and nothing changes. A found
// block leaves its chain, the allocation count drops by one and the free bytes grow by its size.
// The block then merges with the free blocks after it and before it: the merged block keeps the
// first descriptor, and the others go back to the descriptor allocator (those before it zeroed
// first, those after it not). The merged block is linked into the free list between the nearest
// free blocks around it, which also drops the absorbed ones from the list. The error reports
// return, and the code goes on after them, as the game does.
#include <cstdint>

#include "segregated_bi_heap.h"

namespace {

using namespace engine::segregated_bi_heap;

constexpr int32_t line_outside_heap = 0x3e6;
constexpr int32_t line_contradiction = 0x41d;
// "Requested block not marked as allocated. Check and see if you called Free() twice."
RT_GLOBAL(0x04d3ca17, message_not_allocated, const char);
constexpr int32_t line_not_allocated = 0x426;

}  // namespace

extern "C" void bb_engine_segregated_bi_heap_free_02488900(Heap *heap, void *pointer)
{
    const uintptr_t address = address_of(pointer);
    if (!address)
        return;
    if (address < heap->lowest || address >= heap->limit)
        report(line_outside_heap, message_outside_heap.address());

    Descriptor **chain = &heap->bottom_allocations;
    Descriptor *previous;
    Descriptor *block = find_allocation(*chain, address, &previous);
    if (!block) {
        chain = &heap->top_allocations;
        block = find_allocation(*chain, address, &previous);
    }
    if (!block) {
        if (!heap_is_consistent(heap))
            report(line_contradiction, message_contradiction.address());
        report(line_not_allocated, message_not_allocated.address());
        return;
    }
    release_allocation(heap, chain, block, previous);
}
