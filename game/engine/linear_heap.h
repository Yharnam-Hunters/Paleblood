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

}  // namespace engine::linear_heap
