// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the regular heap's aligned allocation (Dantelion2 DLRegularHeap; layout in
// regular_heap.h).
//
// engine_regular_heap_allocate_aligned_02485a60 (0x02485a60) takes a heap, a byte count and an
// alignment and returns a payload on that alignment, or null: for an alignment that is not a
// power of two (zero passes), when the free bytes are fewer than the request, or when no free
// block holds an aligned payload. Alignments up to 16 are the plain allocation's (every payload
// is 16-aligned).
//
// Below 0x400 the first block of the lowest non-empty list above the worst case (request plus
// alignment) is taken unchecked. Otherwise, or when there is none, the lists from the request's
// own up are searched in order for the first block whose first aligned payload still has room.
// A payload that is not aligned already leaves a free block in front of the allocation (in its
// list unless it is only a header); what is left behind the allocation becomes a free block too.
#include <cstdint>

#include "regular_heap.h"
#include "runtime/original.h"

namespace {

using namespace engine::regular_heap;

RT_ORIGINAL(0x02485730, allocate, void *(Heap *, uint64_t request));

constexpr uint64_t largest_unchecked_alignment = 0x3ff;
constexpr uint64_t overlap = offsetof(Block, next_and_flags);   // see block_size_for

uintptr_t align_up(uintptr_t address, uint64_t alignment)
{
    return (address + (alignment - 1)) & (0 - alignment);
}

struct Choice {
    Block *block;
    int index;                  // its list
};

// The first block, from list `first_index` up, whose first aligned payload leaves room for
// `size` bytes before the next block.
Choice first_aligned_fit(Heap *heap, int first_index, uint64_t size, uint64_t alignment)
{
    const uint64_t payload = size - overlap;
    for (uint64_t lists = heap->nonempty_lists & (~uint64_t{0} << first_index); lists; lists &= lists - 1) {
        const int index = __builtin_ctzll(lists);
        Block *sentinel = list_sentinel(heap, index);
        for (Block *block = sentinel->list_next; block != sentinel; block = block->list_next) {
            const uintptr_t next = block->next_and_flags - prev_in_use;
            const uintptr_t aligned = align_up(address_of(block) + block_header_size, alignment);
            if (next > aligned && next - aligned + overlap >= payload)
                return {block, index};
        }
    }
    return {nullptr, 0};
}

}  // namespace

extern "C" void *bb_engine_regular_heap_allocate_aligned_02485a60(Heap *heap, uint64_t request,
                                                                 uint64_t alignment)
{
    constexpr uint64_t plain_alignment = 16;
    if ((alignment & (alignment - 1)) != 0 || heap->available < request)
        return nullptr;
    if (alignment <= plain_alignment)
        return allocate(heap, request);
    const uint64_t size = block_size_for(request);

    Choice choice{nullptr, 0};
    if (alignment <= largest_unchecked_alignment) {
        const uint64_t larger_lists = heap->nonempty_lists & (~uint64_t{1} << list_index(alignment + size));
        if (larger_lists) {
            choice.index = __builtin_ctzll(larger_lists);
            choice.block = list_sentinel(heap, choice.index)->list_next;
        }
    }
    if (!choice.block) {
        choice = first_aligned_fit(heap, list_index(size), size, alignment);
        if (!choice.block)
            return nullptr;
    }

    Block *block = choice.block;
    take_from_list(heap, block, choice.index);
    book_allocation(heap, size);
    const uintptr_t next_word = block->next_and_flags;
    Block *next = reinterpret_cast<Block *>(next_word - prev_in_use);
    const uintptr_t payload = address_of(block) + block_header_size;

    if (payload & (alignment - 1)) {
        // The allocation starts further in; the front stays free.
        const uintptr_t aligned = align_up(payload, alignment);
        Block *allocated = reinterpret_cast<Block *>(aligned - block_header_size);
        Block *rest = reinterpret_cast<Block *>(address_of(allocated) + size);
        block->next_and_flags = address_of(allocated) | prev_in_use;
        allocated->prev = block;
        allocated->next_and_flags = address_of(rest) | block_in_use;
        if (address_of(next) == address_of(rest))
            next->next_and_flags += prev_in_use;
        else
            free_rest(heap, rest, next_word);
        if (address_of(allocated) != payload)
            push_free_block(heap, block, allocated);
        return reinterpret_cast<void *>(aligned);
    }

    Block *rest = reinterpret_cast<Block *>(address_of(block) + size);
    if (address_of(next) <= address_of(rest)) {
        next->next_and_flags += prev_in_use;
        block->next_and_flags = next_word + block_in_use;
    } else {
        block->next_and_flags = address_of(rest) | prev_in_use | block_in_use;
        free_rest(heap, rest, next_word);
    }
    return reinterpret_cast<void *>(payload);
}
