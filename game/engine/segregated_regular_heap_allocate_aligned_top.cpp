// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's aligned allocation from the top (Dantelion2
// DLSegregatedRegularHeap; classes, descriptors and lists in segregated_regular_heap.h).
//
// engine_segregated_regular_heap_allocate_aligned_top_02489fc0 (0x02489fc0) takes a heap, a size
// and an alignment and returns the allocation's address, or 0 when nothing fits or the alignment
// is not a power of two. It did not run in a boot to character creation. Alignments up to 16 are
// a plain allocation from the top. The size is rounded up to 16. The block is the free block at
// the highest address, among those of the size's class and every larger one, that holds the size
// from its first aligned address. The allocation goes at the highest aligned address where it fits;
// the part before it stays free under the block's descriptor, and a new descriptor makes the part
// after it (shorter than the alignment) free.
//
// When the descriptor allocator has no room the block goes back to the list of its whole size and
// the allocation fails, without storing the class bits it changed. If that happens for the part
// after the allocation, the block has already gone into the list of its front part and the
// descriptor for the allocation is lost: the game links the block into a list a second time. Kept.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

struct Highest {
    Descriptor *block = nullptr;
    uintptr_t address = 0;      // 0: none found
    uint64_t size = 0;
    uint64_t bit = 0;           // the class bit of the list it is in
};

uintptr_t align_down(uintptr_t address, uint64_t alignment)
{
    return address & (0 - alignment);
}

// Among the given classes, the free block at the highest address that holds `size` from its first
// aligned address. Each list is walked from its first entry (its bit says it is not empty).
Highest highest_fitting(Heap *heap, uint64_t candidates, uint64_t size, uint64_t alignment)
{
    Highest best;
    for (; candidates; candidates -= lowest_bit(candidates)) {
        ListNode *sentinel = &heap->classes[lowest_class(candidates)];
        ListNode *node = sentinel->next;
        do {
            Descriptor *block = descriptor_of(node);
            const uintptr_t start = block->address;
            if (start > best.address) {
                const uintptr_t end = block->next->address;
                if (end >= align_down(start + alignment - 1, alignment) + size)
                    best = {block, start, end - start, lowest_bit(candidates)};
            }
            node = node->next;
        } while (node != sentinel);
    }
    return best;
}

}  // namespace

extern "C" uintptr_t bb_engine_segregated_regular_heap_allocate_aligned_top_02489fc0(Heap *heap, uint64_t request,
                                                                                   uint64_t alignment)
{
    if (alignment & (alignment - 1))
        return 0;
    if (alignment <= alignment_of_plain)
        return allocate_top(heap, request);
    if (heap->available < request)
        return 0;
    const uint64_t size = round_up_request(request);
    uint64_t mask = heap->class_mask;
    const uint64_t candidates = classes_from(size_class(size)) & mask;
    if (!candidates)
        return 0;
    const Highest best = highest_fitting(heap, candidates, size, alignment);
    if (best.address == 0)
        return 0;

    Descriptor *block = best.block;
    if (list_remove(block))
        mask &= ~best.bit;
    const uintptr_t address = align_down(best.address + best.size - size, alignment);
    const uint64_t front = address - best.address;
    Descriptor *allocated = block;
    if (front == 0) {
        if (best.size > size) {
            Descriptor *rest = descriptor_allocate(&heap->descriptor_pool);
            if (!rest) {
                give_back(heap, block, best.size);
                return 0;
            }
            make_free(heap, rest, best.size - size, mask);
            rest->address = best.address + size;
            insert_after(block, rest);
        }
    } else {
        Descriptor *placed = descriptor_allocate(&heap->descriptor_pool);
        if (!placed) {
            give_back(heap, block, best.size);
            return 0;
        }
        make_free(heap, block, front, mask);
        const uint64_t after = best.size - front - size;
        if (after != 0) {
            Descriptor *rest = descriptor_allocate(&heap->descriptor_pool);
            if (!rest) {
                give_back(heap, block, best.size);
                return 0;
            }
            make_free(heap, rest, after, mask);
            rest->address = address + size;
            insert_after(block, rest);
        }
        placed->address = address;
        insert_after(block, placed);
        allocated = placed;
    }
    mark_in_use(heap, allocated, address);
    commit(heap, mask, size);
    return address;
}
