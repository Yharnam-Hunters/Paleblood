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
    // The descriptor allocator sits at the first 8-aligned address here (descriptor_pool), with
    // the heap's integrity-check switch in the last byte.
    uint8_t unknown_0x00[0x68];
    Descriptor terminator;
    Descriptor *bottom_allocations;
    Descriptor *top_allocations;
    uintptr_t lowest;           // the range a freed address must lie in: [lowest, limit)
    uintptr_t limit;
    uint64_t allocation_count;
    uint64_t available;         // free bytes
};
static_assert(offsetof(Heap, terminator) == 0x68 && offsetof(Heap, bottom_allocations) == 0x90);
static_assert(offsetof(Heap, top_allocations) == 0x98 && offsetof(Heap, lowest) == 0xa0);
static_assert(offsetof(Heap, limit) == 0xa8 && offsetof(Heap, allocation_count) == 0xb0);
static_assert(offsetof(Heap, available) == 0xb8);

// The descriptor allocator's allocate (null when it has no room) and free.
RT_ORIGINAL(0x02484c70, pool_allocate, Descriptor *(void *pool));
RT_ORIGINAL(0x02484de0, pool_free, void(void *pool, Descriptor *descriptor));

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLSegregatedBiHeap.cpp"
RT_GLOBAL(0x04d3c8e8, source_file, const char);
// "Data structure broken. Geographically next blocks must exist."
RT_GLOBAL(0x04d3cb05, message_no_next_block, const char);

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
inline void *descriptor_pool(Heap *heap)
{
    constexpr uint32_t pool_alignment = 8;
    const uint32_t padding = (0u - static_cast<uint32_t>(address_of(heap))) & (pool_alignment - 1);
    return reinterpret_cast<uint8_t *>(heap) + padding;
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

// Links a free descriptor into the free list after `previous`.
inline void free_list_insert_after(Descriptor *previous, Descriptor *block)
{
    Descriptor *following = previous->list_next;
    previous->list_next = block;
    block->list_next = following;
    block->list_prev = previous;
    following->list_prev = block;
}

}  // namespace engine::segregated_bi_heap
