// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the linear heap's allocations (Dantelion2 DLLinearHeap; layout in linear_heap.h).
//
// From the bottom, engine_linear_heap_allocate_02485100 (0x02485100) hands out the next `size`
// bytes and engine_linear_heap_allocate_aligned_02485120 (0x02485120) first pads the bottom to
// the alignment. From the top, engine_linear_heap_allocate_top_02485190 (0x02485190) places a
// block 8-aligned below the top and engine_linear_heap_allocate_aligned_top_024851d0
// (0x024851d0) aligned to at least 8; each block gets its header below it. All return null when
// the bottom and the top would cross (the sums wrap as the game's do), and the aligned forms
// return null for an alignment that is not a power of two (0 counts as one). The steps are in
// linear_heap.h, shared with the temporary heap.
#include <cstdint>

#include "linear_heap.h"

using namespace engine::linear_heap;

extern "C" void *bb_engine_linear_heap_allocate_02485100(Heap *heap, uint64_t size)
{
    return allocate(heap, size);
}

extern "C" void *bb_engine_linear_heap_allocate_aligned_02485120(Heap *heap, uint64_t size, uint64_t alignment)
{
    return allocate_aligned(heap, size, alignment);
}

extern "C" void *bb_engine_linear_heap_allocate_top_02485190(Heap *heap, uint64_t size)
{
    return allocate_top(heap, size);
}

extern "C" void *bb_engine_linear_heap_allocate_aligned_top_024851d0(Heap *heap, uint64_t size, uint64_t alignment)
{
    return allocate_aligned_top(heap, size, alignment);
}
