// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the robust heap's two allocation strategies (Dantelion2 DLRobustHeap; block layout in
// robust_heap.h). Both take a heap and a request in bytes, return the payload or null, and keep
// the heap's free-byte count and allocation count; on failure both counts are put back.
//
// engine_robust_heap_allocate_02486810 (0x02486810) is the ordinary allocation, called from the
// heap's allocation entry points (also the aligned one, 0x02486cd0) and in nearly every frame. It
// takes the best fit from the bottom of a block:
// - a small request (up to 0x1f8 bytes) takes the last block of its own list; else the last block
//   of the next non-empty larger list, split; else the tree's smallest block;
// - a large request takes an exact size from the tree, else the smallest larger block.
// A split leaves the front in use and files the rest by its size. Only when the block was the
// tree's smallest and is bigger than the request by more than 0x200 bytes does the rest take its
// place in the tree directly (it is still the smallest, so the order holds).
//
// engine_robust_heap_allocate_top_02487380 (0x02487380) allocates from the top instead (used by
// the aligned top allocation 0x02487650): of every free block big enough (for a small request:
// its own and every larger list and the whole tree; for a large one: the tree from the smallest
// fitting size up) it takes the one at the highest address and carves the allocation from that
// block's end, so the rest stays at the front.
#include <cstdint>

#include "robust_heap.h"

namespace {

using namespace engine::robust_heap;

// Splits the tree's smallest block when the rest stays big: the rest's node takes over the
// block's links in the tree, and the parent, children and root point at it instead.
void split_keeping_tree_place(Heap *heap, FreeNode *node, uint64_t usable)
{
    Block *rest_block = split_front(block_of(node), usable);
    FreeNode *rest = &rest_block->payload;
    rest->size = free_block_usable(rest_block);
    if (heap->large_root == node)
        heap->large_root = rest;
    FreeNode *left = node->next_or_left;
    FreeNode *right = node->prev_or_right;
    const uintptr_t parent_and_colour = node->parent_and_colour;
    rest->next_or_left = left;
    rest->prev_or_right = right;
    rest->parent_and_colour = parent_and_colour;
    const uintptr_t rest_address = reinterpret_cast<uintptr_t>(rest);
    if (left)
        left->parent_and_colour = (left->parent_and_colour & tree_colour) | rest_address;
    if (right)
        right->parent_and_colour = (right->parent_and_colour & tree_colour) | rest_address;
    FreeNode *parent = reinterpret_cast<FreeNode *>(parent_and_colour & ~tree_colour);
    if (!parent)
        return;
    if (parent->next_or_left == node)
        parent->next_or_left = rest;
    else
        parent->prev_or_right = rest;
}

// Takes a free block that is not linked anywhere any more: splits off the rest and files it.
FreeNode *split_and_file_rest(Heap *heap, FreeNode *node, uint64_t usable)
{
    Block *rest = split_front(block_of(node), usable);
    file_free_block(heap, rest, free_block_usable(rest));
    return node;
}

// Takes the tree's smallest block for a request it fits.
FreeNode *take_tree_smallest(Heap *heap, FreeNode *node, uint64_t usable)
{
    const uint64_t block_usable = free_block_usable(block_of(node));
    if (block_usable == usable) {
        tree_remove(&heap->large_root, node);
        mark_in_use(block_of(node));
    } else if (block_usable > usable + split_in_place_margin) {
        split_keeping_tree_place(heap, node, usable);
    } else {
        tree_remove(&heap->large_root, node);
        split_and_file_rest(heap, node, usable);
    }
    return node;
}

FreeNode *allocate_small(Heap *heap, uint64_t usable)
{
    FreeNode *own = small_bin(heap, usable);
    if (!list_empty(own)) {
        FreeNode *node = own->prev_or_right;
        list_unlink(node);
        mark_in_use(block_of(node));
        return node;
    }
    for (uint64_t index = small_bin_index(usable) + 1; index < small_bin_count; ++index) {
        FreeNode *bin = small_bin_at(heap, index);
        if (list_empty(bin))
            continue;
        FreeNode *node = bin->prev_or_right;
        list_unlink(node);
        return split_and_file_rest(heap, node, usable);
    }
    if (!heap->large_root)
        return nullptr;
    return take_tree_smallest(heap, tree_leftmost(heap->large_root), usable);
}

FreeNode *allocate_large(Heap *heap, uint64_t usable)
{
    // Down the tree: an exact size wins at once; otherwise remember the smallest larger node.
    FreeNode *best = nullptr;
    bool turned_right = false;
    for (FreeNode *at = heap->large_root; at;) {
        if (at->size > usable) {
            best = at;
            at = at->next_or_left;
        } else if (at->size == usable) {
            tree_remove(&heap->large_root, at);
            mark_in_use(block_of(at));
            return at;
        } else {
            turned_right = true;
            at = at->prev_or_right;
        }
    }
    if (!best)
        return nullptr;
    // Only left turns: the best node is the tree's smallest.
    if (!turned_right)
        return take_tree_smallest(heap, best, usable);
    tree_remove(&heap->large_root, best);
    return split_and_file_rest(heap, best, usable);
}

// The free block at the highest address among those that fit (null if none).
Block *highest_fitting_block(Heap *heap, uint64_t request, uint64_t usable)
{
    Block *highest = nullptr;
    auto consider = [&highest](FreeNode *node) {
        if (block_of(node) > highest)
            highest = block_of(node);
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
        // The smallest node that fits.
        for (FreeNode *at = heap->large_root; at;) {
            if (usable <= at->size) {
                first_in_tree = at;
                at = at->next_or_left;
            } else {
                at = at->prev_or_right;
            }
        }
    }
    for (FreeNode *node = first_in_tree; node; node = tree_successor(node))
        consider(node);
    return highest;
}

// Carves an allocation from the end of a free block; the front, if any, stays free.
FreeNode *carve_from_top(Heap *heap, Block *block, uint64_t usable)
{
    const uintptr_t next_and_flags = block->next_and_flags;
    const uint64_t block_usable = free_block_usable(block);
    if (block_usable >= smallest_usable) {
        if (block_usable <= largest_small)
            list_unlink(&block->payload);
        else
            tree_remove(&heap->large_root, &block->payload);
    }
    Block *next = free_block_next(block);
    Block *allocation = reinterpret_cast<Block *>(reinterpret_cast<uintptr_t>(next) - usable - block_overhead);
    next->next_and_flags += prev_in_use;
    const uint64_t front = reinterpret_cast<uintptr_t>(allocation) - reinterpret_cast<uintptr_t>(block);
    if (front == 0) {
        block->next_and_flags = next_and_flags + block_in_use;
        return &allocation->payload;
    }
    // The allocation follows a free block now: in use, without the previous-in-use flag.
    allocation->next_and_flags = reinterpret_cast<uintptr_t>(next) | block_in_use;
    allocation->prev = block;
    block->next_and_flags = reinterpret_cast<uintptr_t>(allocation) | prev_in_use;
    file_free_block(heap, block, front - block_overhead);
    return &allocation->payload;
}

}  // namespace

extern "C" void *bb_engine_robust_heap_allocate_02486810(Heap *heap, uint64_t request)
{
    if (heap->available < request)
        return nullptr;
    const uint64_t count_before = heap->allocation_count++;
    const uint64_t usable = usable_for_request(request);
    heap->available -= usable + block_overhead;
    FreeNode *payload = request > largest_small ? allocate_large(heap, usable) : allocate_small(heap, usable);
    if (!payload) {
        heap->allocation_count = count_before;
        heap->available += usable + block_overhead;
    }
    return payload;
}

extern "C" void *bb_engine_robust_heap_allocate_top_02487380(Heap *heap, uint64_t request)
{
    if (heap->available < request)
        return nullptr;
    const uint64_t count_before = heap->allocation_count++;
    const uint64_t usable = usable_for_request(request);
    heap->available -= usable + block_overhead;
    Block *block = highest_fitting_block(heap, request, usable);
    if (!block) {
        heap->allocation_count = count_before;
        heap->available += usable + block_overhead;
        return nullptr;
    }
    return carve_from_top(heap, block, usable);
}
