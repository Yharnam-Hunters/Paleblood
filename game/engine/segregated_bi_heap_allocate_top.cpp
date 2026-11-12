// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's allocation from the top (Dantelion2 DLSegregatedBiHeap;
// descriptors and lists in segregated_bi_heap.h).
//
// engine_segregated_bi_heap_allocate_top_024882f0 (0x024882f0) takes a heap, a size and an
// alignment and returns the allocation's address, or 0 when nothing fits; the engine's allocator
// front ends use it for allocations meant to stay out of the way of the bottom ones. The size is
// rounded up to 16. The free list is walked from its end (the highest block) for a block that
// holds the size from its first aligned address, the same test as the bottom allocation. A block
// that holds more, or does not start aligned, gives the allocation its top end: a new descriptor
// takes the highest aligned place that holds the size, and the gap left above it (less than the
// alignment) becomes a free block of its own when a second descriptor is available; without one
// the gap stays in the allocation. The block itself stays free, holding what is below. With no
// descriptor at all, the whole block is taken from its first aligned address. The allocation goes
// to the front of the top allocations. The error reports return, and the code goes on after them,
// as the game does.
#include <cstdint>

#include "segregated_bi_heap.h"

namespace {

using namespace engine::segregated_bi_heap;

// "Invalid negative offset size."
RT_GLOBAL(0x04d3cb69, message_negative_offset, const char);
constexpr int32_t line_negative_offset = 0x10f;
constexpr int32_t line_not_aligned = 0x114;
constexpr int32_t line_bad_split = 0x116;

// Carves the top end of a free block into a new block (`top`, already linked after the block in
// address order) of `needed` bytes at the highest address aligned to `alignment` below the block's
// end.
void place_at_top(Heap *heap, Descriptor *block, Descriptor *top, uint64_t needed, uint64_t alignment)
{
    Descriptor *following = top->geo_next;
    const uint64_t mask = alignment - 1;
    // The bytes between the aligned allocation's end and the block's end.
    const uint64_t gap = (following->address - needed) & mask;
    if (gap != 0) {
        Descriptor *gap_block = pool_allocate(descriptor_pool(heap));
        if (gap_block) {
            geo_insert_after(top, gap_block);
            gap_block->list_next = nullptr;
            gap_block->list_prev = nullptr;
            gap_block->address = following->address - gap;
            free_list_insert_after(block, gap_block);
        }
    }
    const uint64_t taken = gap + needed;
    if (taken > following->address)
        report(line_negative_offset, message_negative_offset.address());
    top->address = following->address - taken;
    if (top->address & mask)
        report(line_not_aligned, message_not_aligned.address());
    if (top->address + needed > following->address)
        report(line_bad_split, message_bad_split.address());
}

}  // namespace

extern "C" uintptr_t bb_engine_segregated_bi_heap_allocate_top_024882f0(Heap *heap, uint64_t size,
                                                                        uint64_t alignment)
{
    const uint64_t needed = allocation_size(size);
    Descriptor *terminator = &heap->terminator;
    Descriptor *block = terminator->list_prev;
    Fit found;
    for (;; block = block->list_prev) {
        if (block == terminator)
            return 0;
        found = fit(block, alignment);
        if (found.usable >= needed)
            break;
    }

    heap->allocation_count += 1;
    if (found.front != 0 || found.usable > needed) {
        Descriptor *top = pool_allocate(descriptor_pool(heap));
        if (top) {
            geo_insert_after(block, top);
            top->list_next = nullptr;
            top->list_prev = nullptr;
            place_at_top(heap, block, top, needed, alignment);
            move_to_chain(top, &heap->top_allocations);
            if (top == terminator)
                report(line_terminator, message_terminator.address());
            Descriptor *next = checked_next(top, line_no_next_block);
            heap->available += top->address - next->address;
            return top->address;
        }
        block->address = align_up(block->address, alignment);
    }
    move_to_chain(block, &heap->top_allocations);
    Descriptor *next = checked_next(block, line_no_next_block);
    heap->available += block->address - next->address;
    return block->address;
}
