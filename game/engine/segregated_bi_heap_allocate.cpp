// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's allocation from the bottom (Dantelion2 DLSegregatedBiHeap;
// descriptors and lists in segregated_bi_heap.h).
//
// engine_segregated_bi_heap_allocate_bottom_02488030 (0x02488030) takes a heap, a size and an
// alignment and returns the allocation's address, or 0 when nothing fits (0.8M calls in a boot to
// character creation). The size is rounded up to 16. The first free block, in address order, that
// holds the size from its first aligned address is taken. When it holds more, or does not start
// aligned, two new descriptors split it: the rest after the allocation stays free, and so does the
// part before the aligned address. A descriptor the allocator cannot provide is done without: with
// no descriptor for the rest the whole block is taken (from its aligned address), and with none
// for the front part that part is simply left out of every block. The block leaves the free list
// and goes to the front of the bottom allocations. The error reports return, and the code goes on
// after them, as the game does.
#include <cstdint>

#include "segregated_bi_heap.h"

namespace {

using namespace engine::segregated_bi_heap;

constexpr uint64_t size_granularity = 16;
constexpr int32_t line_walk_no_next_block = 0x2f3;
constexpr int32_t line_no_next_block = 0x310;
// "Logical error. Alignment operation failed."
RT_GLOBAL(0x04d3cb87, message_not_aligned, const char);
constexpr int32_t line_not_aligned = 0xd3;
// "Logical error. Awkward block split."
RT_GLOBAL(0x04d3cbb2, message_bad_split, const char);
constexpr int32_t line_bad_split = 0xd5;
// "Logical Error. Invaid block is given." (sic)
RT_GLOBAL(0x04d3cb43, message_invalid_block, const char);
constexpr int32_t line_invalid_block = 0x1c9;

// Links a new descriptor into the address chain right after `block`.
void geo_insert_after(Descriptor *block, Descriptor *added)
{
    Descriptor *following = block->geo_next;
    block->geo_next = added;
    added->geo_prev = block;
    added->geo_next = following;
    following->geo_prev = added;
}

// Links a new descriptor into the address chain right before `block`.
void geo_insert_before(Descriptor *block, Descriptor *added)
{
    Descriptor *preceding = block->geo_prev;
    preceding->geo_next = added;
    added->geo_prev = preceding;
    added->geo_next = block;
    block->geo_prev = added;
}

// Splits a free block around an allocation of `needed` bytes from its first aligned address: the
// rest after it and the part before it become free blocks of their own, each placed in the free
// list just before the block (which is about to leave it).
void split(Heap *heap, Descriptor *block, uint64_t needed, uint64_t alignment)
{
    void *pool = descriptor_pool(heap);
    Descriptor *rest = pool_allocate(pool);
    if (!rest) {
        block->address = align_up(block->address, alignment);
        return;
    }
    geo_insert_after(block, rest);
    rest->list_next = nullptr;
    rest->list_prev = nullptr;
    // The block starts before its aligned address. (As the heap tests it: alignment minus the
    // misalignment has a bit outside the alignment.)
    const uint64_t misalignment = block->address & (alignment - 1);
    if (~alignment & (alignment - misalignment)) {
        Descriptor *front = pool_allocate(pool);
        if (front) {
            geo_insert_before(block, front);
            front->list_next = nullptr;
            front->list_prev = nullptr;
            front->address = block->address;
            free_list_insert_after(block->list_prev, front);
        }
        block->address = align_up(block->address, alignment);
    }
    rest->address = block->address + needed;
    free_list_insert_after(block->list_prev, rest);
    if (block->address & (alignment - 1))
        report(line_not_aligned, message_not_aligned.address());
    if (block->address + needed > rest->address)
        report(line_bad_split, message_bad_split.address());
}

}  // namespace

extern "C" uintptr_t bb_engine_segregated_bi_heap_allocate_bottom_02488030(Heap *heap, uint64_t size,
                                                                           uint64_t alignment)
{
    const uint64_t needed = (size + size_granularity - 1) & ~(size_granularity - 1);
    Descriptor *terminator = &heap->terminator;
    Descriptor *block = terminator->list_next;
    uint64_t front = 0;
    uint64_t usable = 0;
    for (;; block = block->list_next) {
        if (block == terminator)
            return 0;
        const uintptr_t end = checked_next(block, line_walk_no_next_block)->address;
        const uintptr_t start = align_up(block->address, alignment);
        front = 0;
        usable = 0;
        if (start <= end) {
            front = start - block->address;
            usable = end - start;
        }
        if (usable >= needed)
            break;
    }

    heap->allocation_count += 1;
    if (front != 0 || usable > needed)
        split(heap, block, needed, alignment);
    Descriptor *next = checked_next(block, line_no_next_block);
    heap->available += block->address - next->address;
    if (block->list_prev) {
        if (!block->list_next)
            report(line_invalid_block, message_invalid_block.address());
        block->list_prev->list_next = block->list_next;
        block->list_next->list_prev = block->list_prev;
    }
    block->list_next = heap->bottom_allocations;
    block->list_prev = nullptr;
    heap->bottom_allocations = block;
    return block->address;
}
