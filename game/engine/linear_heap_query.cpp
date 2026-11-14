// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the linear heap's queries (Dantelion2 DLLinearHeap; layout in linear_heap.h).
//
// - engine_linear_heap_check_owned_02485160 (0x02485160) reports an address outside the range.
// - engine_linear_heap_contains_02485330 (0x02485330): whether an address is in the range.
// - engine_linear_heap_walk_02485340 (0x02485340) steps a walk over the heap's regions (a null
//   walk returns false): first the bottom's used memory, then the free gap between the bottom and
//   the top, then each top block from the lowest up, until the end of the range (false). The
//   walk's cursor starts at 0 and holds where the next step reads.
#include <cstddef>
#include <cstdint>

#include "linear_heap.h"

using namespace engine::linear_heap;

namespace {

constexpr int32_t line_not_ours = 0x16e;

struct Walk {
    uintptr_t cursor;           // 0 to start; then lowest, then a top header, then highest
    uintptr_t block;            // the region's start (a top block: 16 bytes below its header)
    uintptr_t memory;           // its memory (a top block: its header, as the game reports it)
    uint64_t size;
    uint64_t usable;            // its size less its header, for a top block
    bool free;
};
static_assert(offsetof(Walk, usable) == 0x20 && offsetof(Walk, free) == 0x28);

}  // namespace

extern "C" void bb_engine_linear_heap_check_owned_02485160(Heap *heap, uintptr_t address)
{
    if (!holds(heap, address))
        engine::fatal_error(source_file.address(), line_not_ours, message_not_ours.address());
}

extern "C" bool bb_engine_linear_heap_contains_02485330(Heap *heap, uintptr_t address)
{
    return holds(heap, address);
}

extern "C" bool bb_engine_linear_heap_walk_02485340(Heap *heap, Walk *walk)
{
    if (!walk)
        return false;
    const uintptr_t at = walk->cursor;
    if (at == 0) {
        // The bottom's used memory.
        walk->cursor = heap->lowest;
        walk->block = heap->lowest;
        walk->memory = heap->lowest;
        walk->size = heap->bottom - heap->lowest;
        walk->usable = heap->bottom - heap->lowest;
        walk->free = false;
        return true;
    }
    if (at == heap->lowest) {
        // The free gap.
        walk->cursor = heap->top;
        walk->block = heap->bottom;
        walk->memory = heap->bottom;
        walk->size = heap->top - heap->bottom;
        walk->usable = heap->top - heap->bottom;
        walk->free = true;
        return true;
    }
    if (at == heap->highest)
        return false;
    // A top block, by its header.
    const TopHeader *header = header_at(at);
    walk->cursor = header->above;
    walk->block = at - sizeof(TopHeader);
    walk->memory = at;
    const uint64_t size = header->above - at;
    walk->size = size;
    walk->usable = size - sizeof(TopHeader);
    walk->free = header->in_use == 0;
    return true;
}
