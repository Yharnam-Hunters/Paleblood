// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the regular heap's allocation (Dantelion2 DLRegularHeap; block layout in regular_heap.h).
//
// engine_regular_heap_allocate_02485730 (0x02485730) takes a heap and a byte count and returns a
// 16-aligned payload, or null when the heap's free bytes are fewer than the request or no free
// block is large enough. The block size serving the request is block_size_for(request). The
// first block of the lowest non-empty list above the size's own list fits whatever its size;
// failing that, the size's own list is searched in order for the first block at least that
// large. The block leaves its list; when more than the block size is left over, the rest becomes
// a free block in its list (a rest of only a header stays out of the lists), otherwise the whole
// block is handed out and the next block learns that the one before it is in use.
#include <cstdint>

#include "regular_heap.h"

namespace {

using namespace engine::regular_heap;

// The first block of list `index` holding at least `size` bytes, or null. A free block's word
// is the next block's address with prev_in_use, so the test is on that word as the game makes it.
Block *first_fit(Heap *heap, int index, uint64_t size)
{
    constexpr uint64_t word_bias = block_header_size - offsetof(Block, next_and_flags) + prev_in_use;
    const uint64_t payload = size - offsetof(Block, next_and_flags);
    Block *sentinel = list_sentinel(heap, index);
    for (Block *block = sentinel->list_next; block != sentinel; block = block->list_next) {
        if (block->next_and_flags - address_of(block) - word_bias >= payload)
            return block;
    }
    return nullptr;
}

}  // namespace

extern "C" void *bb_engine_regular_heap_allocate_02485730(Heap *heap, uint64_t request)
{
    if (heap->available < request)
        return nullptr;
    const uint64_t size = block_size_for(request);
    int index = list_index(size);
    Block *block;
    const uint64_t larger_lists = heap->nonempty_lists & (~uint64_t{1} << index);
    if (larger_lists) {
        index = __builtin_ctzll(larger_lists);
        block = list_sentinel(heap, index)->list_next;
    } else {
        if (!((heap->nonempty_lists >> index) & 1))
            return nullptr;
        block = first_fit(heap, index, size);
        if (!block)
            return nullptr;
    }

    const uintptr_t next_word = block->next_and_flags;
    Block *next = reinterpret_cast<Block *>(next_word - prev_in_use);
    take_from_list(heap, block, index);
    book_allocation(heap, size);
    Block *rest = reinterpret_cast<Block *>(address_of(block) + size);
    if (address_of(next) <= address_of(rest)) {
        next->next_and_flags += prev_in_use;
        block->next_and_flags = next_word + block_in_use;
    } else {
        block->next_and_flags = address_of(rest) | prev_in_use | block_in_use;
        free_rest(heap, rest, next_word);
    }
    return reinterpret_cast<uint8_t *>(block) + block_header_size;
}
