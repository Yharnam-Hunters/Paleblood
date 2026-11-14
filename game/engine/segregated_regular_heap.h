// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's segregated regular heap (Dantelion2 DLSegregatedRegularHeap): blocks described
// outside the memory they manage, free blocks kept in lists by size class.
//
// Every block of the heap's range has a descriptor: its start address and its neighbours in
// address order (a block ends where the next one starts). The descriptors come from a fixed-size
// allocator inside the heap object, so the managed memory holds no headers.
//
// A free block is in the list of its size class: 64 doubly linked rings through sentinels in the
// heap, with one bit per class in class_mask while its list is not empty. Classes 0 to 31 hold one
// size each (16 to 0x200 bytes); from class 32 on, each holds a power-of-two range ([0x200, 0x400)
// is class 32 apart from 0x200 itself). A block in use is in a hash table of 0x44f buckets keyed by
// its address, through the word that is the list's next link while it is free; its list prev word
// is null.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "fixed_allocator.h"
#include "runtime/original.h"

namespace engine::segregated_regular_heap {

struct ListNode {
    ListNode *next;
    ListNode *prev;
};

struct Descriptor {
    Descriptor *next;           // neighbours in address order
    Descriptor *prev;
    uintptr_t address;          // where the block starts
    ListNode node;              // free: its class list; in use: next = the hash bucket's next, prev = null
};
static_assert(offsetof(Descriptor, address) == 0x10 && offsetof(Descriptor, node) == 0x18);
static_assert(sizeof(Descriptor) == 0x28);

constexpr uint64_t class_count = 64;
constexpr uint64_t hash_buckets = 0x44f;

// The heap's state (part of the DLSegregatedRegularHeap object).
struct Heap {
    uint64_t available;         // free bytes
    uint64_t capacity;          // the free bytes of the empty heap
    uint64_t class_mask;        // bit c: the list of class c is not empty
    uint64_t allocation_count;
    Descriptor end;             // closes the address ring: in use, its address the range's end
    Descriptor **hash_table;    // hash_buckets chains of blocks in use
    uintptr_t range_start;
    uintptr_t range_end;
    ListNode classes[class_count];
    fixed_allocator::Allocator descriptor_pool;
};
static_assert(offsetof(Heap, class_mask) == 0x10 && offsetof(Heap, allocation_count) == 0x18);
static_assert(offsetof(Heap, end) == 0x20 && offsetof(Heap, hash_table) == 0x48);
static_assert(offsetof(Heap, range_start) == 0x50 && offsetof(Heap, classes) == 0x60);
static_assert(offsetof(Heap, descriptor_pool) == 0x460);

// The descriptor allocator (fixed_allocator_allocate.cpp, fixed_allocator_free.cpp): null when
// it has no room.
RT_ORIGINAL(0x02484c70, descriptor_allocate, Descriptor *(fixed_allocator::Allocator *pool));
RT_ORIGINAL(0x02484de0, descriptor_free, void(fixed_allocator::Allocator *pool, Descriptor *descriptor));

// The plain allocations, which the aligned ones use for alignments of 16 or less.
RT_ORIGINAL(0x02489620, allocate, uintptr_t(Heap *, uint64_t request));
RT_ORIGINAL(0x02489d20, allocate_top, uintptr_t(Heap *, uint64_t request));

constexpr uint64_t size_granularity = 16;
constexpr uint64_t largest_exact_size = 0x200;      // classes 0..31 hold exactly 16 * (c + 1)
constexpr uint64_t first_range_class = 23;          // class of [2^k, 2^(k+1)) is k + 23 (k >= 9)
constexpr uint64_t class_shift_mask = 63;           // shifts use the count's low six bits, as x86 does
constexpr uint64_t alignment_of_plain = 16;         // aligned requests up to this use the plain ones

inline uint64_t round_up_request(uint64_t request)
{
    return (request + (size_granularity - 1)) & ~(size_granularity - 1);
}

// The size class of a block size (or of a request rounded to 16). A size of 0 gives a huge class,
// of which the shifts below use the low six bits (63), as the game's do.
inline uint64_t size_class(uint64_t size)
{
    if (size <= largest_exact_size)
        return (size - 1) / size_granularity;
    const uint64_t floor_log2 = 63 - static_cast<uint64_t>(__builtin_clzll(size));
    return floor_log2 + first_range_class;
}

inline uint64_t class_bit(uint64_t cls)
{
    return uint64_t{1} << (cls & class_shift_mask);
}

// The bits of this class and every larger one.
inline uint64_t classes_from(uint64_t cls)
{
    return ~uint64_t{0} << (cls & class_shift_mask);
}

inline unsigned lowest_class(uint64_t classes)
{
    return static_cast<unsigned>(__builtin_ctzll(classes));
}

inline uint64_t lowest_bit(uint64_t classes)
{
    return classes & (0 - classes);
}

inline Descriptor *descriptor_of(ListNode *node)
{
    return reinterpret_cast<Descriptor *>(reinterpret_cast<uintptr_t>(node) - offsetof(Descriptor, node));
}

inline uint64_t block_size(const Descriptor *block)
{
    return block->next->address - block->address;
}

// Takes a block out of its class list; true when the list is now empty.
inline bool list_remove(Descriptor *block)
{
    ListNode *next = block->node.next;
    ListNode *prev = block->node.prev;
    next->prev = prev;
    prev->next = next;
    return next == prev;
}

// Puts a free block at the front of a class list; true when the list was empty (the caller sets
// the class's bit, except where the game does not).
inline bool list_push_front(Heap *heap, uint64_t cls, Descriptor *block)
{
    ListNode *sentinel = &heap->classes[cls];
    ListNode *first = sentinel->next;
    sentinel->next = &block->node;
    block->node.prev = sentinel;
    block->node.next = first;
    first->prev = &block->node;
    return first == sentinel;
}

// Puts a free block in the list of its size's class and sets the class's bit in `mask`.
inline void make_free(Heap *heap, Descriptor *block, uint64_t size, uint64_t &mask)
{
    const uint64_t cls = size_class(size);
    if (list_push_front(heap, cls, block))
        mask |= class_bit(cls);
}

// Puts a block back in its size's class list after a descriptor ran out, without touching the
// class bits (the caller returns without storing the mask it changed, as the game does).
inline void give_back(Heap *heap, Descriptor *block, uint64_t size)
{
    list_push_front(heap, size_class(size), block);
}

// Links a new descriptor into the address chain right after `block`.
inline void insert_after(Descriptor *block, Descriptor *added)
{
    Descriptor *following = block->next;
    block->next = added;
    added->prev = block;
    added->next = following;
    following->prev = added;
}

inline Descriptor *&hash_bucket(Heap *heap, uintptr_t address)
{
    return heap->hash_table[address % hash_buckets];
}

// The bucket chain runs through node.next while a block is in use.
inline Descriptor *hash_next(const Descriptor *block)
{
    return reinterpret_cast<Descriptor *>(block->node.next);
}

// Marks a block in use (null list prev) and adds it to the front of its address's bucket.
inline void mark_in_use(Heap *heap, Descriptor *block, uintptr_t address)
{
    block->node.prev = nullptr;
    Descriptor *&bucket = hash_bucket(heap, address);
    block->node.next = reinterpret_cast<ListNode *>(bucket);
    bucket = block;
}

inline bool is_free(const Descriptor *block)
{
    return block->node.prev != nullptr;
}

// Books an allocation of `size` bytes once the lists are settled.
inline void commit(Heap *heap, uint64_t mask, uint64_t size)
{
    heap->class_mask = mask;
    heap->available -= size;
    heap->allocation_count += 1;
}

}  // namespace engine::segregated_regular_heap
