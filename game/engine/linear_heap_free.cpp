// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: giving linear heap memory back (Dantelion2 DLLinearHeap; layout in linear_heap.h).
//
// - engine_linear_heap_free_02485230 (0x02485230) frees a top block (null is ignored; an address
//   outside the range is reported and then treated like any other). Freeing the lowest top block
//   moves the top up to the next block still in use, or to the end of the range; freeing another
//   block only marks it free.
// - engine_linear_heap_free_until_024852d0 (0x024852d0) rewinds the bottom to a mark (an earlier
//   bottom) when the bottom is past it; a mark below the start or past the end is reported.
// The steps are in linear_heap.h, shared with the temporary heap.
#include <cstdint>

#include "linear_heap.h"

using namespace engine::linear_heap;

extern "C" void bb_engine_linear_heap_free_02485230(Heap *heap, void *memory)
{
    free_top(heap, memory);
}

extern "C" void bb_engine_linear_heap_free_until_024852d0(Heap *heap, uintptr_t mark)
{
    free_until(heap, mark);
}
