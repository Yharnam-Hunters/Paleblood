// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's segregated bi-heap (Dantelion2 DLSegregatedBiHeap): blocks described outside the
// memory they manage, allocated from both ends.
//
// Every block of the heap's range has a descriptor: its start address, its neighbours in address
// order (from which its size follows) and two list words. The descriptors come from a fixed-size
// allocator (Dantelion2 DLFixedAllocator) inside the heap object, so the managed memory holds no
// headers. A terminator descriptor inside the heap closes the address chain at both ends.
//
// A free block is in the free list, doubly linked in address order through the terminator; its
// list_prev is never null, which is how the code tells a free block from one in use. A block in
// use has a null list_prev and is in one of two singly linked allocation chains: allocations from
// the bottom of the range or from its top.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "fixed_allocator.h"
#include "runtime/original.h"

namespace engine::segregated_bi_heap {

struct Descriptor {
    uintptr_t address;          // where the block starts; it ends where geo_next starts
    Descriptor *geo_prev;       // neighbours in address order
    Descriptor *geo_next;
    Descriptor *list_prev;      // free: the free list's previous; in use: null
    Descriptor *list_next;      // free: the free list's next; in use: the next in its chain
};
static_assert(sizeof(Descriptor) == 0x28 && offsetof(Descriptor, list_next) == 0x20);

// The heap's state (part of the DLSegregatedBiHeap object).
struct Heap {
    // The descriptor allocator sits at the first 8-aligned address here (descriptor_pool).
    uint8_t unknown_0x00[0x67];
    bool initialized;           // set up and not finalized: the descriptor allocator is live
    Descriptor terminator;
    Descriptor *bottom_allocations;
    Descriptor *top_allocations;
    uintptr_t lowest;           // the range a freed address must lie in: [lowest, limit)
    uintptr_t limit;
    uint64_t allocation_count;
    uint64_t available;         // free bytes
};
static_assert(offsetof(Heap, initialized) == 0x67);
static_assert(offsetof(Heap, terminator) == 0x68 && offsetof(Heap, bottom_allocations) == 0x90);
static_assert(offsetof(Heap, top_allocations) == 0x98 && offsetof(Heap, lowest) == 0xa0);
static_assert(offsetof(Heap, limit) == 0xa8 && offsetof(Heap, allocation_count) == 0xb0);
static_assert(offsetof(Heap, available) == 0xb8);

// The descriptor allocator's allocate (null when it has no room) and free.
RT_ORIGINAL(0x02484c70, pool_allocate, Descriptor *(fixed_allocator::Allocator *pool));
RT_ORIGINAL(0x02484de0, pool_free, void(fixed_allocator::Allocator *pool, Descriptor *descriptor));

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLSegregatedBiHeap.cpp"
RT_GLOBAL(0x04d3c8e8, source_file, const char);
// "Data structure broken. Geographically next blocks must exist."
RT_GLOBAL(0x04d3cb05, message_no_next_block, const char);
// "Logical Error. Invaid block is given." (sic)
RT_GLOBAL(0x04d3cb43, message_invalid_block, const char);
constexpr int32_t line_invalid_block = 0x1c9;
// "Tried to get size of terminator."
RT_GLOBAL(0x04d3cae4, message_terminator, const char);
constexpr int32_t line_terminator = 0x30c;
constexpr int32_t line_no_next_block = 0x310;
// "Logical error. Alignment operation failed."
RT_GLOBAL(0x04d3cb87, message_not_aligned, const char);
// "Logical error. Awkward block split."
RT_GLOBAL(0x04d3cbb2, message_bad_split, const char);
// "Tried to free block where not taken from this heap."
RT_GLOBAL(0x04d3c951, message_outside_heap, const char);
// "DLSegregatedBiHeap internal state contradiction. ..."
RT_GLOBAL(0x04d3c985, message_contradiction, const char);

// The size an allocation takes: the request rounded up to 16.
inline uint64_t allocation_size(uint64_t size)
{
    constexpr uint64_t size_granularity = 16;
    return (size + size_granularity - 1) & ~(size_granularity - 1);
}

// The heap's integrity check (true when the heap is consistent or not set up;
// segregated_bi_heap_check.cpp).
RT_ORIGINAL(0x02488cf0, heap_is_consistent, bool(Heap *));

inline uintptr_t address_of(const void *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

// The engine's error report, with this file's name; it returns.
inline void report(int32_t line, const char *message)
{
    engine::fatal_error(source_file.address(), line, message);
}

// The descriptor allocator: at the heap's first 8-aligned address (the heap computes the padding
// from the address's low 32 bits).
inline fixed_allocator::Allocator *descriptor_pool(Heap *heap)
{
    constexpr uint32_t pool_alignment = 8;
    const uint32_t padding = (0u - static_cast<uint32_t>(address_of(heap))) & (pool_alignment - 1);
    return reinterpret_cast<fixed_allocator::Allocator *>(reinterpret_cast<uint8_t *>(heap) + padding);
}
static_assert(sizeof(fixed_allocator::Allocator) + 7 <= offsetof(Heap, initialized));

// Gives the descriptor allocator's chunks back and forgets them (set-up failure and tear-down).
inline void tear_down_descriptor_pool(Heap *heap)
{
    fixed_allocator::Allocator *pool = descriptor_pool(heap);
    if (pool->chunk_tree)
        fixed_allocator::release_chunks(pool, pool->chunk_tree);
    fixed_allocator::forget_chunks(pool);
    heap->initialized = false;
}

inline bool is_free(const Descriptor *block)
{
    return block->list_prev != nullptr;
}

// The block after this one in address order, reported (at `line`) when missing; the report
// returns, and the caller goes on with the null it read.
inline Descriptor *checked_next(Descriptor *block, int32_t line)
{
    Descriptor *next = block->geo_next;
    if (!next)
        report(line, message_no_next_block.address());
    return next;
}

// Rounds an address up to the alignment (as the heap does: plus alignment - 1, masked).
inline uintptr_t align_up(uintptr_t address, uint64_t alignment)
{
    return (address + (alignment - 1)) & (0 - alignment);
}

// How a free block holds an allocation from its first aligned address: the bytes before that
// address and the bytes from it to the block's end (both 0 when the address is past the end).
struct Fit {
    uint64_t front;
    uint64_t usable;
};

inline Fit fit(Descriptor *block, uint64_t alignment)
{
    constexpr int32_t line_walk_no_next_block = 0x2f3;
    const uintptr_t end = checked_next(block, line_walk_no_next_block)->address;
    const uintptr_t start = align_up(block->address, alignment);
    if (start > end)
        return {0, 0};
    return {start - block->address, end - start};
}

// Links a free descriptor into the free list after `previous`.
inline void free_list_insert_after(Descriptor *previous, Descriptor *block)
{
    Descriptor *following = previous->list_next;
    previous->list_next = block;
    block->list_next = following;
    block->list_prev = previous;
    following->list_prev = block;
}

// Links a new descriptor into the address chain right after `block`.
inline void geo_insert_after(Descriptor *block, Descriptor *added)
{
    Descriptor *following = block->geo_next;
    block->geo_next = added;
    added->geo_prev = block;
    added->geo_next = following;
    following->geo_prev = added;
}

// Takes a block being allocated out of the free list (when it is in it) and puts it at the front
// of an allocation chain. A free block without a list successor is reported first.
inline void move_to_chain(Descriptor *block, Descriptor **chain)
{
    if (block->list_prev) {
        if (!block->list_next)
            report(line_invalid_block, message_invalid_block.address());
        block->list_prev->list_next = block->list_next;
        block->list_next->list_prev = block->list_prev;
    }
    block->list_next = *chain;
    block->list_prev = nullptr;
    *chain = block;
}

// The allocation starting at `address` in the chain from `head`, and the one before it in the
// chain (null for the first); null when the chain has none.
inline Descriptor *find_allocation(Descriptor *head, uintptr_t address, Descriptor **previous)
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
// and returns the descriptor of the merged block. The merged block keeps the first descriptor;
// the others go back to the descriptor allocator (those before it zeroed first, those after it
// not).
inline Descriptor *merge_neighbours(Heap *heap, Descriptor *block)
{
    fixed_allocator::Allocator *pool = descriptor_pool(heap);
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
inline void free_list_insert(Descriptor *block)
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

// Frees an allocation found in `chain` after `previous` (null for the chain's first): it leaves
// the chain, the allocation count drops by one and the free bytes grow by its size; it then
// merges with the free blocks around it and is linked into the free list, which also drops the
// absorbed ones from the list.
inline void release_allocation(Heap *heap, Descriptor **chain, Descriptor *block, Descriptor *previous)
{
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

}  // namespace engine::segregated_bi_heap
