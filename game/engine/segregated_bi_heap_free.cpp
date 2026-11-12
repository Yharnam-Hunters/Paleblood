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

// "Tried to free block where not taken from this heap."
RT_GLOBAL(0x04d3c951, message_outside_heap, const char);
constexpr int32_t line_outside_heap = 0x3e6;
// "DLSegregatedBiHeap internal state contradiction. ..."
RT_GLOBAL(0x04d3c985, message_contradiction, const char);
constexpr int32_t line_contradiction = 0x41d;
// "Requested block not marked as allocated. Check and see if you called Free() twice."
RT_GLOBAL(0x04d3ca17, message_not_allocated, const char);
constexpr int32_t line_not_allocated = 0x426;
// "Tried to get size of terminator."
RT_GLOBAL(0x04d3cae4, message_terminator, const char);
constexpr int32_t line_terminator = 0x30c;
constexpr int32_t line_no_next_block = 0x310;

// The heap's integrity check (true when the heap is consistent or its checks are off).
RT_ORIGINAL(0x02488cf0, heap_is_consistent, bool(Heap *));

// The allocation starting at `address` in the chain from `head`, and the one before it in the
// chain (null for the first); null when the chain has none.
Descriptor *find_allocation(Descriptor *head, uintptr_t address, Descriptor **previous)
{
    *previous = nullptr;
    for (Descriptor *block = head; block; block = block->list_next) {
        if (block->address == address)
            return block;
        *previous = block;
    }
    return nullptr;
}

// Merges a block that just became free with free neighbours (the terminator stops both walks),
// and returns the descriptor of the merged block.
Descriptor *merge_neighbours(Heap *heap, Descriptor *block)
{
    void *pool = descriptor_pool(heap);
    Descriptor *terminator = &heap->terminator;
    Descriptor *after = block->geo_next;
    while (is_free(after) && after != terminator) {
        Descriptor *absorbed = after;
        after = absorbed->geo_next;
        pool_free(pool, absorbed);
    }
    for (Descriptor *before = block->geo_prev; is_free(before) && before != terminator;
         before = block->geo_prev) {
        Descriptor *absorbed = block;
        block = before;
        *absorbed = Descriptor{};
        pool_free(pool, absorbed);
    }
    if (after != block->geo_next) {
        block->geo_next = after;
        after->geo_prev = block;
    }
    return block;
}

// Links a free block into the free list between the nearest free blocks before and after it in
// address order.
void free_list_insert(Descriptor *block)
{
    Descriptor *following = block;
    do
        following = following->geo_next;
    while (!is_free(following));
    Descriptor *preceding = block;
    do
        preceding = preceding->geo_prev;
    while (!is_free(preceding));
    preceding->list_next = block;
    block->list_next = following;
    block->list_prev = preceding;
    following->list_prev = block;
}

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

    if (block == *chain)
        *chain = block->list_next;
    heap->allocation_count -= 1;
    if (block == &heap->terminator)
        report(line_terminator, message_terminator.address());
    Descriptor *next = checked_next(block, line_no_next_block);
    heap->available += next->address - block->address;
    if (previous)
        previous->list_next = block->list_next;
    free_list_insert(merge_neighbours(heap, block));
}
