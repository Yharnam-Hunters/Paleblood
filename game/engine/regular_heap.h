// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's regular heap (Dantelion2 DLRegularHeap): its blocks and its 64 free lists.
//
// The heap's memory is a chain of blocks. A block starts with two words: a pointer to the block
// before it (valid only while that block is free) and the address of the next block with two
// flags in the low bits (this block in use; the block before it in use). A free block also holds
// the links of its free list in its next two words.
//
// Free blocks are filed by their whole size (next - block) into one of 64 circular lists with
// sentinels: list k holds the sizes from 2^k up to 2^(k+1) - 1, and bit k of a mask says that list
// is not empty. A free block of only the two header words (16 bytes) has no room for links and
// sits in no list until it is merged.
#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::regular_heap {

struct Block {
    Block *prev;                // valid only while the block before this one is free
    uintptr_t next_and_flags;
    Block *list_prev;           // free blocks only: the neighbours in their list
    Block *list_next;
};
static_assert(offsetof(Block, next_and_flags) == 0x8 && offsetof(Block, list_prev) == 0x10);
static_assert(offsetof(Block, list_next) == 0x18);

constexpr uintptr_t block_in_use = 0x1;
constexpr uintptr_t prev_in_use = 0x2;
constexpr uintptr_t block_flags = block_in_use | prev_in_use;
constexpr uint64_t block_header_size = offsetof(Block, list_prev);  // the payload starts here
constexpr int list_count = 64;

// A list's sentinel: only the two links of a block, so 16 bytes each.
struct ListLinks {
    Block *prev;                // the list's last block
    Block *next;                // the list's first block
};

// The heap's state (part of the DLRegularHeap object).
struct Heap {
    uint64_t nonempty_lists;    // bit k: list k holds a block
    uintptr_t lowest_payload;   // the range a payload passed to free must lie in
    uintptr_t highest_payload;
    uint64_t available;         // free bytes, block headers included
    uint64_t allocation_count;
    ListLinks lists[list_count];
};
static_assert(offsetof(Heap, lowest_payload) == 0x8 && offsetof(Heap, highest_payload) == 0x10);
static_assert(offsetof(Heap, available) == 0x18 && offsetof(Heap, allocation_count) == 0x20);
static_assert(offsetof(Heap, lists) == 0x28 && sizeof(Heap) == 0x428);

inline uintptr_t address_of(const void *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

// The list a free block of this whole size belongs in: the index of its highest set bit.
inline int list_index(uint64_t size)
{
    constexpr int highest_bit = 63;
    return highest_bit - __builtin_clzll(size);
}

// The sentinel of list `index`, seen as a block: its links sit where a block's links do.
inline Block *list_sentinel(Heap *heap, int index)
{
    return reinterpret_cast<Block *>(address_of(&heap->lists[index]) - offsetof(Block, list_prev));
}

// Takes a free block of this size out of its list; the list's bit goes when it empties. A 16-byte
// block is in no list.
inline void unlink_free_block(Heap *heap, Block *block, Block *next)
{
    if (address_of(block) + block_header_size == address_of(next))
        return;
    Block *prev_in_list = block->list_prev;
    Block *next_in_list = block->list_next;
    prev_in_list->list_next = next_in_list;
    next_in_list->list_prev = prev_in_list;
    if (next_in_list == prev_in_list)
        heap->nonempty_lists &= ~(uint64_t{1} << list_index(address_of(next) - address_of(block)));
}

// Puts a free block at the front of the list for its size, and sets the list's bit when the list
// was empty.
inline void push_free_block(Heap *heap, Block *block, Block *next)
{
    const int index = list_index(address_of(next) - address_of(block));
    Block *sentinel = list_sentinel(heap, index);
    Block *first = sentinel->list_next;
    sentinel->list_next = block;
    block->list_prev = sentinel;
    block->list_next = first;
    first->list_prev = block;
    if (first == sentinel)
        heap->nonempty_lists |= uint64_t{1} << index;
}

}  // namespace engine::regular_heap
