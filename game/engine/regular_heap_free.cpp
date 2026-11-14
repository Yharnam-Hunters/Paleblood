// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the regular heap's free (Dantelion2 DLRegularHeap; block layout in regular_heap.h).
//
// engine_regular_heap_free_024858a0 (0x024858a0) takes a heap and a payload (null does nothing).
// It is the game's most called heap function during boot, reached through the heap's vtable and
// from the engine's allocator front ends. A payload outside the heap's range and a block not in
// use (a double free) are reported through the engine's error report, which returns, and the
// block is freed anyway, as the game does. The free bytes grow by the block's size and the
// allocation count drops by one. The block merges with a free block before it and a free block
// after it (each leaves its list first), and the merged block goes to the front of its list.
#include <cstdint>

#include "engine.h"
#include "regular_heap.h"

namespace {

using namespace engine::regular_heap;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLRegularHeap.cpp"
RT_GLOBAL(0x04d3c6d4, source_file, const char);
// "given memory block does not seem to belong here.(Begin 0x%p End 0x%p Address 0x%p)"
RT_GLOBAL(0x04d3c738, message_outside_heap, const char);
constexpr int32_t line_outside_heap = 699;
// "given memory block seems to be improper or freed already."
RT_GLOBAL(0x04d3cc44, message_not_in_use, const char);
constexpr int32_t line_not_in_use = 710;

}  // namespace

extern "C" void bb_engine_regular_heap_free_024858a0(Heap *heap, void *payload)
{
    if (!payload)
        return;
    const uintptr_t lowest = heap->lowest_payload;
    const uintptr_t highest = heap->highest_payload;
    if (address_of(payload) < lowest || address_of(payload) > highest)
        engine::fatal_error(source_file.address(), line_outside_heap, message_outside_heap.address(),
                            lowest, highest, payload);
    Block *block = reinterpret_cast<Block *>(address_of(payload) - block_header_size);
    const uintptr_t next_and_flags = block->next_and_flags;
    Block *next = reinterpret_cast<Block *>(next_and_flags & ~block_flags);
    if (!(next_and_flags & block_in_use))
        engine::fatal_error(source_file.address(), line_not_in_use, message_not_in_use.address());

    heap->available += address_of(next) - address_of(block);
    heap->allocation_count -= 1;

    Block *start = block;
    if (!(next_and_flags & prev_in_use)) {
        start = block->prev;
        unlink_free_block(heap, start, block);
        // The block's own in-use flag is cleared, though it is inside the merged block now.
        block->next_and_flags = next_and_flags - block_in_use;
    }
    const uintptr_t next_next_and_flags = next->next_and_flags;
    Block *end;
    if (next_next_and_flags & block_in_use) {
        // The next block stays in use and now follows a free block.
        start->next_and_flags = address_of(next) | prev_in_use;
        next->prev = start;
        next->next_and_flags = next_next_and_flags - prev_in_use;
        end = next;
    } else {
        // The next block is free too, so it follows a block in use: its flags are prev_in_use.
        start->next_and_flags = next_next_and_flags;
        end = reinterpret_cast<Block *>(next_next_and_flags - prev_in_use);
        end->prev = start;
        unlink_free_block(heap, next, end);
    }
    push_free_block(heap, start, end);
}
