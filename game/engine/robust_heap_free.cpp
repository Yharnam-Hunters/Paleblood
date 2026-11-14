// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the robust heap's free (Dantelion2 DLRobustHeap; block layout in robust_heap.h).
//
// engine_robust_heap_free_02487b50 (0x02487b50) takes a heap and a payload (null does nothing),
// called from the heap's free entry points about as often as the allocations. It reports a block
// that is not in use (a double free) and a payload outside the heap's range through the engine's
// error report, which returns, and then frees the block anyway, as the game does. The free bytes
// grow by the block's size and the allocation count drops by one. The block merges with a free
// block before it and a free block after it (each leaves its list or the tree first), and the
// merged block is filed by its size; a block in use after it loses its previous-in-use flag.
#include <cstdint>

#include "engine.h"
#include "robust_heap.h"

namespace {

using namespace engine::robust_heap;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLRobustHeap.cpp"
RT_GLOBAL(0x04d3c78b, source_file, const char);
// A block not in use was passed to Free; possibly a double free (%p).
RT_GLOBAL(0x04d3c832, message_double_free, const char);
constexpr int32_t line_double_free = 0x793;
// A pointer outside the heap's range (0x%p) was passed to Free.
RT_GLOBAL(0x04d3c8a1, message_outside_heap, const char);
constexpr int32_t line_outside_heap = 0x79a;

uintptr_t address_of(const void *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

uint64_t usable_between(const Block *block, const Block *next)
{
    return address_of(next) - address_of(block) - block_overhead;
}

}  // namespace

extern "C" void bb_engine_robust_heap_free_02487b50(Heap *heap, void *payload)
{
    if (!payload)
        return;
    Block *block = reinterpret_cast<Block *>(address_of(payload) - block_header_size);
    const uintptr_t next_and_flags = block->next_and_flags;
    if (!(next_and_flags & block_in_use))
        engine::fatal_error(source_file.address(), line_double_free, message_double_free.address(), payload);
    if (address_of(payload) < heap->lowest_payload || address_of(payload) > heap->highest_payload)
        engine::fatal_error(source_file.address(), line_outside_heap, message_outside_heap.address(), payload);

    Block *next = reinterpret_cast<Block *>(next_and_flags & ~(block_in_use | prev_in_use));
    const uintptr_t next_next_and_flags = next->next_and_flags;
    heap->available += address_of(next) - address_of(block);
    heap->allocation_count -= 1;

    Block *start = block;
    if (!(next_and_flags & prev_in_use)) {
        start = block->prev;
        unlink_free_block(heap, start, usable_between(start, block));
    }
    Block *end;
    if (next_next_and_flags & block_in_use) {
        // The next block stays in use; it now follows a free block. (It followed one in use, so
        // the flag is there to take away.)
        next->next_and_flags = next_next_and_flags - prev_in_use;
        end = next;
    } else {
        // The next block is free too: it follows a block in use, so its flags are prev_in_use.
        end = reinterpret_cast<Block *>(next_next_and_flags - prev_in_use);
        unlink_free_block(heap, next, usable_between(next, end));
    }
    end->prev = start;
    start->next_and_flags = address_of(end) | prev_in_use;
    file_free_block(heap, start, usable_between(start, end));
}
