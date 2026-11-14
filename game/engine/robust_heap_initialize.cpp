// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the robust heap's set-up (Dantelion2 DLRobustHeap; layout in robust_heap.h).
//
// engine_robust_heap_initialize_024863f0 (0x024863f0) takes a heap and the memory it is to manage
// and returns false for no memory, for 0x1f8 bytes or fewer, or when the memory ends before the
// first block would have room. The small lists' sentinels come first, at the memory's start
// rounded up to 128 bytes; the first block follows them and runs to the memory's
// end rounded down to 16, less the closing header-only block (in use, after a free block, with
// no successor). That one free block is the heap's capacity and free bytes, and goes into its
// small list or the tree; a block with less than the smallest usable size goes nowhere.
#include <cstdint>

#include "robust_heap.h"

namespace {

using namespace engine::robust_heap;

constexpr uint64_t smallest_memory = 0x1f9;
constexpr uintptr_t sentinel_alignment = 0x80;
constexpr uintptr_t block_alignment = 0x10;
// The first block plus the room the memory must have past the sentinels.
constexpr uint64_t room_after_sentinels = 0x26e;

uintptr_t align_up(uintptr_t address, uintptr_t alignment)
{
    return (address + alignment - 1) & ~(alignment - 1);
}

}  // namespace

extern "C" bool bb_engine_robust_heap_initialize_024863f0(Heap *heap, void *memory, uint64_t size)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(memory);
    if (size < smallest_memory || !memory)
        return false;
    const uintptr_t sentinels = align_up(start, sentinel_alignment);
    if (sentinels + room_after_sentinels >= start + size)
        return false;

    heap->small_bins = reinterpret_cast<uint8_t *>(sentinels);
    for (uint64_t index = 0; index < small_bin_count; ++index) {
        FreeNode *sentinel = small_bin_at(heap, index);
        sentinel->prev_or_right = sentinel;
        sentinel->next_or_left = sentinel;
    }
    heap->large_root = nullptr;

    Block *first = reinterpret_cast<Block *>(sentinels + small_bin_count * small_bin_stride);
    Block *end = reinterpret_cast<Block *>(((start + size) & ~(block_alignment - 1)) - block_header_size);
    first->next_and_flags = reinterpret_cast<uintptr_t>(end) | prev_in_use;
    end->prev = first;
    end->next_and_flags = block_in_use;
    heap->lowest_payload = reinterpret_cast<uintptr_t>(first);
    heap->highest_payload = reinterpret_cast<uintptr_t>(end);
    const uint64_t free_bytes = reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(first);
    heap->capacity = free_bytes;
    heap->available = free_bytes;
    heap->allocation_count = 0;
    file_free_block(heap, first, free_bytes - block_overhead);
    return true;
}
