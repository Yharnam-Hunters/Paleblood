// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's allocation (Dantelion2 DLSegregatedRegularHeap; classes,
// descriptors and lists in segregated_regular_heap.h).
//
// engine_segregated_regular_heap_allocate_02489620 (0x02489620) takes a heap and a size and returns
// the allocation's address, or 0 when nothing fits (68K calls in a boot to character creation).
// The size is rounded up to 16 (0 becomes 16). The block is the first of the smallest non-empty
// class whose every block is large enough; failing that, for sizes past 0x200, the first block of
// the size's own class that is large enough. The allocation takes the block's start, and a new
// descriptor makes the rest a free block of its own. When the descriptor allocator has no room
// the block goes back to its class list and the allocation fails, but the game has already put the
// block in the hash table and drops the class bits it changed: both kept here.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

// The first block of the size's own class that holds `size` (classes past 31 hold a range of
// sizes), or null; clears the class's bit in `mask` when it empties the list.
Descriptor *take_fitting_in_own_class(Heap *heap, uint64_t size, uint64_t &mask)
{
    const uint64_t cls = size_class(size);
    if (!(mask & class_bit(cls)))
        return nullptr;
    ListNode *sentinel = &heap->classes[cls];
    for (ListNode *node = sentinel->next; node != sentinel; node = node->next) {
        Descriptor *block = descriptor_of(node);
        if (size <= block_size(block)) {
            if (list_remove(block))
                mask &= ~class_bit(cls);
            return block;
        }
    }
    return nullptr;
}

}  // namespace

extern "C" uintptr_t bb_engine_segregated_regular_heap_allocate_02489620(Heap *heap, uint64_t request)
{
    if (heap->available < request)
        return 0;
    uint64_t size = round_up_request(request);
    uint64_t mask = heap->class_mask;

    // Every block of a class past the size's own (or of its own, for exact classes) fits.
    const bool exact = size <= largest_exact_size;
    if (exact && size == 0)
        size = size_granularity;
    const uint64_t first_class = exact ? size_class(size) : size_class(size) + 1;
    const uint64_t larger = classes_from(first_class) & mask;
    Descriptor *block;
    if (larger) {
        const uint64_t bit = lowest_bit(larger);
        block = descriptor_of(heap->classes[lowest_class(larger)].next);
        if (list_remove(block))
            mask &= ~bit;
    } else {
        // (For an exact class, `larger` already covered the own class: its bit is clear.)
        block = take_fitting_in_own_class(heap, size, mask);
        if (!block)
            return 0;
    }

    const uint64_t available_size = block_size(block);
    const uintptr_t address = block->address;
    mark_in_use(heap, block, address);
    if (available_size > size) {
        Descriptor *rest = descriptor_allocate(&heap->descriptor_pool);
        if (!rest) {
            give_back(heap, block, available_size);
            return 0;
        }
        rest->address = address + size;
        make_free(heap, rest, available_size - size, mask);
        insert_after(block, rest);
    }
    commit(heap, mask, size);
    return address;
}
