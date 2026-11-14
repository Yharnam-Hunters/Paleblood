// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's aligned allocation (Dantelion2 DLSegregatedRegularHeap;
// classes, descriptors and lists in segregated_regular_heap.h).
//
// engine_segregated_regular_heap_allocate_aligned_024898c0 (0x024898c0) takes a heap, a size and an
// alignment and returns the allocation's address, or 0 when nothing fits or the alignment is not
// a power of two (85K calls in a boot to character creation). Alignments up to 16 are a plain
// allocation. The size is rounded up to 16. For alignments up to 0x400, the first block of the
// smallest class whose every block holds the size plus alignment - 1 is taken; otherwise (or with
// none) the classes from the size's own up are searched in order for the first block that holds
// the size from its first aligned address. The allocation starts there; the part before it stays
// free under the block's own descriptor, and a new descriptor makes the rest after it free.
//
// When the descriptor allocator has no room the block goes back to the list of its whole size and
// the allocation fails, without storing the class bits it changed. If that happens for the rest
// after an unaligned start, the block has already gone into the list of its front part and the
// descriptor for the allocation is lost: the game links the block into a list a second time. Kept.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

// Alignments up to this take the first block of a class that holds the worst case.
constexpr uint64_t largest_worst_case_alignment = 0x400;

struct Choice {
    Descriptor *block;
    uint64_t bit;               // its class's bit
};

uintptr_t align_up(uintptr_t address, uint64_t alignment)
{
    return (address + (alignment - 1)) & (0 - alignment);
}

// The first block, by class and then list order, that holds `size` from its first aligned address.
// Each list is walked from its first entry without a check for an empty one: a class bit is only
// set while its list has a block.
Choice search(Heap *heap, uint64_t size, uint64_t alignment, uint64_t mask)
{
    for (uint64_t candidates = classes_from(size_class(size)) & mask; candidates;
         candidates -= lowest_bit(candidates)) {
        ListNode *sentinel = &heap->classes[lowest_class(candidates)];
        ListNode *node = sentinel->next;
        do {
            Descriptor *block = descriptor_of(node);
            if (block->next->address >= align_up(block->address, alignment) + size)
                return {block, lowest_bit(candidates)};
            node = node->next;
        } while (node != sentinel);
    }
    return {nullptr, 0};
}

}  // namespace

extern "C" uintptr_t bb_engine_segregated_regular_heap_allocate_aligned_024898c0(Heap *heap, uint64_t request,
                                                                               uint64_t alignment)
{
    if (alignment & (alignment - 1))
        return 0;
    if (alignment <= alignment_of_plain)
        return allocate(heap, request);
    if (heap->available < request)
        return 0;
    const uint64_t size = round_up_request(request);
    uint64_t mask = heap->class_mask;

    Choice choice{nullptr, 0};
    if (alignment - 1 < largest_worst_case_alignment) {
        const uint64_t larger = classes_from(size_class(size + alignment - 1) + 1) & mask;
        if (larger)
            choice = {descriptor_of(heap->classes[lowest_class(larger)].next), lowest_bit(larger)};
    }
    if (!choice.block) {
        choice = search(heap, size, alignment, mask);
        if (!choice.block)
            return 0;
    }

    Descriptor *block = choice.block;
    const uintptr_t start = block->address;
    const uint64_t whole = block_size(block);
    if (list_remove(block))
        mask &= ~choice.bit;

    Descriptor *allocated = block;
    uintptr_t address = start;
    if (start & (alignment - 1)) {
        Descriptor *aligned = descriptor_allocate(&heap->descriptor_pool);
        if (!aligned) {
            give_back(heap, block, whole);
            return 0;
        }
        const uint64_t front = (0 - start) & (alignment - 1);
        address = align_up(start, alignment);
        make_free(heap, block, front, mask);
        if (whole > front + size) {
            Descriptor *rest = descriptor_allocate(&heap->descriptor_pool);
            if (!rest) {
                give_back(heap, block, whole);
                return 0;
            }
            make_free(heap, rest, whole - front - size, mask);
            rest->address = address + size;
            insert_after(block, rest);
        }
        aligned->address = address;
        insert_after(block, aligned);
        allocated = aligned;
    } else if (whole > size) {
        Descriptor *rest = descriptor_allocate(&heap->descriptor_pool);
        if (!rest) {
            give_back(heap, block, whole);
            return 0;
        }
        make_free(heap, rest, whole - size, mask);
        rest->address = start + size;
        insert_after(block, rest);
    }
    mark_in_use(heap, allocated, address);
    commit(heap, mask, size);
    return address;
}
