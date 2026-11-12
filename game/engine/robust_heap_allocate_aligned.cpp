// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the robust heap's aligned allocations (Dantelion2 DLRobustHeap; block layout in
// robust_heap.h). Both take a heap, a request in bytes and an alignment, and return a payload at
// that alignment or null: null too for an alignment that is not a power of two. Alignments up to
// 16 need nothing special (every payload is 16-aligned) and go to the plain strategy. The heap's
// free-byte and allocation counts are kept as in the plain strategies, and put back on failure.
//
// engine_robust_heap_allocate_aligned_02486cd0 (0x02486cd0) looks from the bottom, like 0x02486810:
// a small request walks its own list from the end for a block already aligned (taken whole), then
// each larger list from the end, then the size tree from its smallest block; a large request walks
// the tree from the smallest block that fits. The first block that works wins: one whose payload
// is aligned is used from its start (split if bigger), else an aligned payload is placed inside it
// at least 0x20 bytes in, if it still ends inside the block.
//
// engine_robust_heap_allocate_aligned_top_02487650 (0x02487650) looks from the top, like 0x02487380:
// of every candidate block (the same lists and tree) it takes the one at the highest address where
// the highest aligned payload that ends inside it starts no earlier than its own payload.
//
// Placing a payload inside a block leaves up to three blocks: a free one in front, the allocation,
// and a free one after it unless the allocation reaches the block's end; the free ones are filed by
// their size.
#include <cstdint>

#include "robust_heap.h"

namespace {

using namespace engine::robust_heap;

// Alignments up to this are what every payload has anyway.
constexpr uint64_t largest_plain_alignment = 0x10;
// An aligned payload placed inside a block starts at least this far after the block's own payload:
// room for a free block in front of the allocation with the smallest usable size.
constexpr uint64_t smallest_front_block = smallest_usable + block_overhead;

uintptr_t address_of(const void *pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

bool is_aligned(const FreeNode *node, uint64_t alignment)
{
    return (address_of(node) & (alignment - 1)) == 0;
}

uintptr_t align_up(uintptr_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

uintptr_t align_down(uintptr_t value, uint64_t alignment)
{
    return value & ~(alignment - 1);
}

// The aligned payload the bottom strategy places inside a block whose own payload is not aligned.
uintptr_t aligned_payload_inside(const FreeNode *node, uint64_t alignment)
{
    return align_up(address_of(node) + smallest_front_block, alignment);
}

// Whether an allocation with this payload ends inside the (free) block.
bool ends_inside(const Block *block, uintptr_t payload, uint64_t usable)
{
    return payload + usable - block_overhead <= address_of(free_block_next(block));
}

// Places an allocation at `payload` inside a free block that is already unlinked: the block's
// front stays free, then the allocation, then (unless the allocation reaches the next block) a
// free rest. Returns the payload.
void *place_inside(Heap *heap, Block *block, uintptr_t payload, uint64_t usable)
{
    const uintptr_t next_and_flags = block->next_and_flags;
    Block *next = free_block_next(block);
    Block *allocation = reinterpret_cast<Block *>(payload - block_header_size);
    Block *rest = reinterpret_cast<Block *>(payload + usable - block_overhead);
    allocation->prev = block;
    block->next_and_flags = address_of(allocation) | prev_in_use;
    const uint64_t front_usable = address_of(allocation) - address_of(block) - block_overhead;
    if (rest == next) {
        // The allocation follows a free block: in use without the previous-in-use flag.
        allocation->next_and_flags = next_and_flags - prev_in_use + block_in_use;
        next->next_and_flags |= prev_in_use;
        file_free_block(heap, block, front_usable);
        return reinterpret_cast<void *>(payload);
    }
    allocation->next_and_flags = address_of(rest) | block_in_use;
    file_free_block(heap, block, front_usable);
    rest->next_and_flags = next_and_flags;
    file_free_block(heap, rest, free_block_usable(rest));
    next->prev = rest;
    return reinterpret_cast<void *>(payload);
}

// Walks the tree in size order from `node` and allocates from the first block that works.
void *aligned_from_tree(Heap *heap, FreeNode *node, uint64_t usable, uint64_t alignment)
{
    for (; node; node = tree_successor(node)) {
        Block *block = block_of(node);
        if (is_aligned(node, alignment)) {
            tree_remove(&heap->large_root, node);
            take_block_start(heap, block, usable);
            return node;
        }
        const uintptr_t payload = aligned_payload_inside(node, alignment);
        if (ends_inside(block, payload, usable)) {
            tree_remove(&heap->large_root, node);
            return place_inside(heap, block, payload, usable);
        }
    }
    return nullptr;
}

void *aligned_small(Heap *heap, uint64_t usable, uint64_t alignment)
{
    // Its own list: only a block that is aligned already will do (it is exactly the right size).
    FreeNode *own = small_bin(heap, usable);
    for (FreeNode *node = own->prev_or_right; node != own; node = node->prev_or_right) {
        if (is_aligned(node, alignment)) {
            list_unlink(node);
            mark_in_use(block_of(node));
            return node;
        }
    }
    for (uint64_t index = small_bin_index(usable) + 1; index < small_bin_count; ++index) {
        FreeNode *bin = small_bin_at(heap, index);
        for (FreeNode *node = bin->prev_or_right; node != bin; node = node->prev_or_right) {
            Block *block = block_of(node);
            if (is_aligned(node, alignment)) {
                list_unlink(node);
                take_block_start(heap, block, usable);
                return node;
            }
            const uintptr_t payload = aligned_payload_inside(node, alignment);
            if (ends_inside(block, payload, usable)) {
                list_unlink(node);
                return place_inside(heap, block, payload, usable);
            }
        }
    }
    if (!heap->large_root)
        return nullptr;
    return aligned_from_tree(heap, tree_leftmost(heap->large_root), usable, alignment);
}

// The tree's smallest node of at least `usable` bytes (null if none).
FreeNode *tree_lower_bound(Heap *heap, uint64_t usable)
{
    FreeNode *found = nullptr;
    for (FreeNode *at = heap->large_root; at;) {
        if (usable <= at->size) {
            found = at;
            at = at->next_or_left;
        } else {
            at = at->prev_or_right;
        }
    }
    return found;
}

// The highest aligned payload for this allocation that still ends inside the block.
uintptr_t top_aligned_payload(const Block *block, uint64_t usable, uint64_t alignment)
{
    const uintptr_t top_payload = address_of(free_block_next(block)) + block_header_size - block_overhead - usable;
    return align_down(top_payload, alignment);
}

// The block at the highest address whose top aligned payload is not before its own payload.
Block *highest_aligned_block(Heap *heap, uint64_t request, uint64_t usable, uint64_t alignment)
{
    Block *highest = nullptr;
    auto consider = [&](FreeNode *node) {
        Block *block = block_of(node);
        if (block > highest && address_of(node) <= top_aligned_payload(block, usable, alignment))
            highest = block;
    };
    FreeNode *first_in_tree = nullptr;
    if (request <= largest_small) {
        for (uint64_t index = small_bin_index(usable); index < small_bin_count; ++index) {
            FreeNode *bin = small_bin_at(heap, index);
            for (FreeNode *node = bin->prev_or_right; node != bin; node = node->prev_or_right)
                consider(node);
        }
        if (heap->large_root)
            first_in_tree = tree_leftmost(heap->large_root);
    } else {
        first_in_tree = tree_lower_bound(heap, usable);
    }
    for (FreeNode *node = first_in_tree; node; node = tree_successor(node))
        consider(node);
    return highest;
}

}  // namespace

extern "C" void *bb_engine_robust_heap_allocate_aligned_02486cd0(Heap *heap, uint64_t request, uint64_t alignment)
{
    if (alignment & (alignment - 1))
        return nullptr;
    if (alignment <= largest_plain_alignment)
        return allocate(heap, request);
    if (heap->available < request)
        return nullptr;
    const uint64_t count_before = heap->allocation_count++;
    const uint64_t usable = usable_for_request(request);
    heap->available -= usable + block_overhead;
    void *payload;
    if (request <= largest_small) {
        payload = aligned_small(heap, usable, alignment);
    } else {
        FreeNode *first = tree_lower_bound(heap, usable);
        payload = first ? aligned_from_tree(heap, first, usable, alignment) : nullptr;
    }
    if (!payload) {
        heap->allocation_count = count_before;
        heap->available += usable + block_overhead;
    }
    return payload;
}

extern "C" void *bb_engine_robust_heap_allocate_aligned_top_02487650(Heap *heap, uint64_t request, uint64_t alignment)
{
    if (alignment & (alignment - 1))
        return nullptr;
    if (alignment <= largest_plain_alignment)
        return allocate_top(heap, request);
    if (heap->available < request)
        return nullptr;
    const uint64_t count_before = heap->allocation_count++;
    const uint64_t usable = usable_for_request(request);
    heap->available -= usable + block_overhead;
    Block *block = highest_aligned_block(heap, request, usable, alignment);
    if (!block) {
        heap->allocation_count = count_before;
        heap->available += usable + block_overhead;
        return nullptr;
    }
    unlink_free_block(heap, block, free_block_usable(block));
    const uintptr_t payload = top_aligned_payload(block, usable, alignment);
    if (payload == address_of(&block->payload)) {
        take_block_start(heap, block, usable);
        return reinterpret_cast<void *>(payload);
    }
    return place_inside(heap, block, payload, usable);
}
