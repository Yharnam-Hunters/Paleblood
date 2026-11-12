// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the linear heap's allocations (Dantelion2 DLLinearHeap; layout in linear_heap.h).
//
// From the bottom, engine_linear_heap_allocate_02485100 (0x02485100) hands out the next `size`
// bytes and engine_linear_heap_allocate_aligned_02485120 (0x02485120) first pads the bottom to
// the alignment. From the top, engine_linear_heap_allocate_top_02485190 (0x02485190) places a
// block 8-aligned below the top and engine_linear_heap_allocate_aligned_top_024851d0
// (0x024851d0) aligned to at least 8; each block gets its header below it. All return null when
// the bottom and the top would cross (the sums wrap as the game's do), and the aligned forms
// return null for an alignment that is not a power of two (0 counts as one).
#include <cstdint>

#include "linear_heap.h"

using namespace engine::linear_heap;

namespace {

bool is_power_of_two_or_zero(uint64_t value)
{
    return (value & (value - 1)) == 0;
}

// Places a top block of `size` bytes with `padding` above it: its header goes below it and the
// top moves down to the header. Null when the header would go below the bottom.
void *place_top(Heap *heap, uint64_t size, uint64_t padding)
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

}  // namespace

extern "C" void *bb_engine_linear_heap_allocate_02485100(Heap *heap, uint64_t size)
{
    const uintptr_t start = heap->bottom;
    const uintptr_t end = start + size;
    if (end > heap->top)
        return nullptr;
    heap->bottom = end;
    return reinterpret_cast<void *>(start);
}

extern "C" void *bb_engine_linear_heap_allocate_aligned_02485120(Heap *heap, uint64_t size, uint64_t alignment)
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

extern "C" void *bb_engine_linear_heap_allocate_top_02485190(Heap *heap, uint64_t size)
{
    constexpr uint64_t alignment_mask = 7;
    return place_top(heap, size, (heap->top - size) & alignment_mask);
}

extern "C" void *bb_engine_linear_heap_allocate_aligned_top_024851d0(Heap *heap, uint64_t size, uint64_t alignment)
{
    constexpr uint64_t smallest_alignment = 8;
    if (!is_power_of_two_or_zero(alignment))
        return nullptr;
    const uint64_t alignment_mask = alignment >= smallest_alignment ? alignment - 1 : smallest_alignment - 1;
    return place_top(heap, size, (heap->top - size) & alignment_mask);
}
