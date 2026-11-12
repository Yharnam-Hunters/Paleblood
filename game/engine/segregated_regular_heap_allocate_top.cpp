// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's allocation from the top (Dantelion2
// DLSegregatedRegularHeap; classes, descriptors and lists in segregated_regular_heap.h).
//
// engine_segregated_regular_heap_allocate_top_02489d20 (0x02489d20) takes a heap and a size and
// returns the allocation's address, or 0 when nothing fits. It did not run in a boot to character
// creation. The size is rounded up to 16 (0 stays 0). The block is the free block at the highest
// address among those of every class larger than the size's own, and of the size's own class
// those that hold the size (classes of 16 and 32 bytes are taken as a whole). The allocation takes
// the block's end, and the front stays free under the block's descriptor, in the list of its new
// size. When the descriptor allocator has no room the block goes back to the list of its whole
// size and the allocation fails, without storing the class bits it changed.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

// Sizes up to this take their own class like the larger ones, without checking the block's size.
constexpr uint64_t largest_unchecked_size = 0x20;

struct Highest {
    Descriptor *block = nullptr;
    uintptr_t address = 0;      // 0: none found
    uint64_t size = 0;
    uint64_t bit = 0;           // the class bit of the list it is in
};

}  // namespace

extern "C" uintptr_t bb_engine_segregated_regular_heap_allocate_top_02489d20(Heap *heap, uint64_t request)
{
    if (heap->available < request)
        return 0;
    const uint64_t size = round_up_request(request);
    uint64_t mask = heap->class_mask;
    const uint64_t own_class = size_class(size);
    const uint64_t own_bit = class_bit(own_class);
    uint64_t candidates = classes_from(own_class) & mask;

    Highest best;
    best.bit = own_bit;
    if (size > largest_unchecked_size && (own_bit & mask)) {
        // The own class's list is walked from its first entry (its bit says it is not empty).
        ListNode *sentinel = &heap->classes[own_class];
        ListNode *node = sentinel->next;
        do {
            Descriptor *block = descriptor_of(node);
            if (block->address > best.address && block_size(block) >= size) {
                best.block = block;
                best.address = block->address;
                best.size = block_size(block);
            }
            node = node->next;
        } while (node != sentinel);
        candidates -= own_bit;
    }
    for (; candidates; candidates -= lowest_bit(candidates)) {
        ListNode *sentinel = &heap->classes[lowest_class(candidates)];
        ListNode *node = sentinel->next;
        do {
            Descriptor *block = descriptor_of(node);
            if (block->address > best.address)
                best = {block, block->address, block_size(block), lowest_bit(candidates)};
            node = node->next;
        } while (node != sentinel);
    }
    if (best.address == 0)
        return 0;

    Descriptor *block = best.block;
    if (list_remove(block))
        mask &= ~best.bit;
    Descriptor *allocated = block;
    uintptr_t address = best.address;
    if (best.size > size) {
        Descriptor *top = descriptor_allocate(&heap->descriptor_pool);
        if (!top) {
            give_back(heap, block, best.size);
            return 0;
        }
        const uint64_t front = best.size - size;
        address = best.address + front;
        top->address = address;
        make_free(heap, block, front, mask);
        insert_after(block, top);
        allocated = top;
    }
    mark_in_use(heap, allocated, address);
    commit(heap, mask, size);
    return address;
}
