// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's linear heap (Dantelion2 DLLinearHeap): a range handed out from both ends.
//
// The bottom grows upwards with no bookkeeping: it is only ever rewound (to a mark, or to the
// start). The top grows downwards in blocks, each with a 16-byte header just below its memory:
// the address the top had before it (the block above, or the end of the range) and whether it is
// in use. Freeing the lowest top block moves the top up past every block that is no longer in
// use; freeing any other block only marks it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace engine::linear_heap {

struct TopHeader {              // just below a top block's memory
    uintptr_t above;            // the top before this block
    uint64_t in_use;
};
static_assert(sizeof(TopHeader) == 0x10);

struct Heap {
    uintptr_t lowest;           // the range: [lowest, highest)
    uintptr_t highest;
    uintptr_t bottom;           // the bottom's end: free memory starts here
    uintptr_t top;              // the lowest top block's header, or highest
};
static_assert(offsetof(Heap, bottom) == 0x10 && offsetof(Heap, top) == 0x18);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLLinearHeap.cpp"
RT_GLOBAL(0x04d3c62d, source_file, const char);
// "given memory block does not seem to belong here."
RT_GLOBAL(0x04d3c801, message_not_ours, const char);
// "DLLinearHeap::FreeUntil() got mark that doesn't belong to its heap."
RT_GLOBAL(0x04d3c690, message_bad_mark, const char);

inline TopHeader *header_at(uintptr_t address)
{
    return reinterpret_cast<TopHeader *>(address);
}

inline bool holds(const Heap *heap, uintptr_t address)
{
    return heap->lowest <= address && address < heap->highest;
}

constexpr int32_t line_free_not_ours = 0x1e0;
constexpr int32_t line_bad_mark = 0x233;

// The heap's operations. DLLinearHeap's own methods and the per-thread temporary heap's (which
// runs one of these heaps) are each these same steps.

inline bool is_power_of_two_or_zero(uint64_t value)
{
    return (value & (value - 1)) == 0;
}

// The next `size` bytes from the bottom; null when the bottom would pass the top.
inline void *allocate(Heap *heap, uint64_t size)
{
    const uintptr_t start = heap->bottom;
    const uintptr_t end = start + size;
    if (end > heap->top)
        return nullptr;
    heap->bottom = end;
    return reinterpret_cast<void *>(start);
}

// The same with the bottom first padded to the alignment; null for an alignment that is not a
// power of two (0 counts as one). The sums wrap as the game's do.
inline void *allocate_aligned(Heap *heap, uint64_t size, uint64_t alignment)
{
    if (!is_power_of_two_or_zero(alignment))
        return nullptr;
    const uintptr_t bottom = heap->bottom;
    // The distance up to the next aligned address (0 when already aligned).
    const uint64_t padding = (alignment - (bottom & (alignment - 1))) & ~alignment;
    const uintptr_t end = size + padding + bottom;
    if (end > heap->top)
        return nullptr;
    heap->bottom = end;
    return reinterpret_cast<void *>(bottom + padding);
}

// Places a top block of `size` bytes with `padding` above it: its header goes below it and the
// top moves down to the header. Null when the header would go below the bottom.
inline void *place_top(Heap *heap, uint64_t size, uint64_t padding)
{
    const uint64_t taken = padding + size;
    const uintptr_t header = heap->top - sizeof(TopHeader) - taken;
    if (heap->bottom > header)
        return nullptr;
    const uintptr_t old_top = heap->top;
    header_at(header)->above = old_top;
    header_at(header)->in_use = 1;
    heap->top = header;
    return reinterpret_cast<void *>(old_top - taken);
}

constexpr uint64_t smallest_top_alignment = 8;

// A top block 8-aligned below the top.
inline void *allocate_top(Heap *heap, uint64_t size)
{
    return place_top(heap, size, (heap->top - size) & (smallest_top_alignment - 1));
}

// A top block aligned to at least 8; null for an alignment that is not a power of two.
inline void *allocate_aligned_top(Heap *heap, uint64_t size, uint64_t alignment)
{
    if (!is_power_of_two_or_zero(alignment))
        return nullptr;
    const uint64_t alignment_mask =
        alignment >= smallest_top_alignment ? alignment - 1 : smallest_top_alignment - 1;
    return place_top(heap, size, (heap->top - size) & alignment_mask);
}

// Frees a top block (null is ignored; an address outside the range is reported and then treated
// like any other). Freeing the lowest top block moves the top up to the next block still in use,
// or to the end of the range; freeing another block only marks it free.
inline void free_top(Heap *heap, void *memory)
{
    if (!memory)
        return;
    const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
    if (!holds(heap, address))
        fatal_error(source_file.address(), line_free_not_ours, message_not_ours.address());
    const uintptr_t header = address - sizeof(TopHeader);
    if (header != heap->top) {
        header_at(header)->in_use = 0;
        return;
    }
    const uintptr_t end = heap->highest;
    uintptr_t above = header;
    do {
        above = header_at(above)->above;
    } while (above != end && header_at(above)->in_use == 0);
    heap->top = above;
}

// Rewinds the bottom to a mark (an earlier bottom) when the bottom is past it; a mark below the
// start or past the end is reported.
inline void free_until(Heap *heap, uintptr_t mark)
{
    if (mark < heap->lowest || mark > heap->highest)
        fatal_error(source_file.address(), line_bad_mark, message_bad_mark.address());
    if (heap->bottom > mark)
        heap->bottom = mark;
}

}  // namespace engine::linear_heap
