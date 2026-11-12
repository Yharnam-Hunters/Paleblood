// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: giving linear heap memory back (Dantelion2 DLLinearHeap; layout in linear_heap.h).
//
// - engine_linear_heap_free_02485230 (0x02485230) frees a top block (null is ignored; an address
//   outside the range is reported and then treated like any other). Freeing the lowest top block
//   moves the top up to the next block still in use, or to the end of the range; freeing another
//   block only marks it free.
// - engine_linear_heap_free_until_024852d0 (0x024852d0) rewinds the bottom to a mark (an earlier
//   bottom) when the bottom is past it; a mark below the start or past the end is reported.
#include <cstdint>

#include "linear_heap.h"

using namespace engine::linear_heap;

namespace {

constexpr int32_t line_free_not_ours = 0x1e0;
constexpr int32_t line_bad_mark = 0x233;

}  // namespace

extern "C" void bb_engine_linear_heap_free_02485230(Heap *heap, void *memory)
{
    if (!memory)
        return;
    const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
    if (!holds(heap, address))
        engine::fatal_error(source_file.address(), line_free_not_ours, message_not_ours.address());
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

extern "C" void bb_engine_linear_heap_free_until_024852d0(Heap *heap, uintptr_t mark)
{
    if (mark < heap->lowest || mark > heap->highest)
        engine::fatal_error(source_file.address(), line_bad_mark, message_bad_mark.address());
    if (heap->bottom > mark)
        heap->bottom = mark;
}
