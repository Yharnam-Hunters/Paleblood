// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated regular heap's free (Dantelion2 DLSegregatedRegularHeap; classes,
// descriptors and lists in segregated_regular_heap.h).
//
// engine_segregated_regular_heap_free_0248a400 (0x0248a400) takes a heap and an address the heap
// handed out (0 does nothing); 528 calls in a boot to character creation. The block is looked up
// in the hash table and leaves it; an address not found there is reported (the report returns and
// the game then reads through the null it found). The block merges with a free neighbour on
// either side, whose descriptors go back to the descriptor allocator (the block's own before the
// next block's), and the result goes to the front of its size's class list.
#include <cstdint>

#include "segregated_regular_heap.h"

namespace {

using namespace engine::segregated_regular_heap;

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLSegregatedRegularHeap.cpp"
RT_GLOBAL(0x04d3cbd6, source_file, const char);
// "given memory block seems to be improper or freed already."
RT_GLOBAL(0x04d3cc44, message_not_allocated, const char);
constexpr int32_t line_not_allocated = 0x48d;

// Finds the block in use at `address` and takes it out of its hash bucket.
Descriptor *unhash(Heap *heap, uintptr_t address)
{
    Descriptor *&bucket = hash_bucket(heap, address);
    Descriptor *previous = nullptr;
    Descriptor *block = bucket;
    for (;;) {
        if (!block)
            engine::fatal_error(source_file.address(), line_not_allocated, message_not_allocated.address());
        if (block->address == address)
            break;
        previous = block;
        block = hash_next(block);
    }
    if (previous)
        previous->node.next = block->node.next;
    else
        bucket = hash_next(block);
    return block;
}

// Takes a free neighbour out of its class list (its size given), clearing the class bit when its
// list empties.
void unlink_free(Descriptor *block, uint64_t size, uint64_t &mask)
{
    if (list_remove(block))
        mask &= ~class_bit(size_class(size));
}

}  // namespace

extern "C" void bb_engine_segregated_regular_heap_free_0248a400(Heap *heap, uintptr_t address)
{
    if (!address)
        return;
    Descriptor *block = unhash(heap, address);
    Descriptor *next = block->next;
    uint64_t mask = heap->class_mask;
    heap->available += next->address - block->address;
    heap->allocation_count -= 1;

    Descriptor *prev = block->prev;
    Descriptor *merged = block;
    uint64_t size;
    if (is_free(next)) {
        Descriptor *after = next->next;
        unlink_free(next, after->address - next->address, mask);
        if (is_free(prev)) {
            unlink_free(prev, block->address - prev->address, mask);
            prev->next = after;
            after->prev = prev;
            descriptor_free(&heap->descriptor_pool, block);
            merged = prev;
        } else {
            block->next = after;
            after->prev = block;
        }
        size = after->address - merged->address;
        descriptor_free(&heap->descriptor_pool, next);
    } else if (is_free(prev)) {
        unlink_free(prev, block->address - prev->address, mask);
        prev->next = next;
        next->prev = prev;
        size = next->address - prev->address;
        descriptor_free(&heap->descriptor_pool, block);
        merged = prev;
    } else {
        size = next->address - block->address;
    }
    make_free(heap, merged, size, mask);
    heap->class_mask = mask;
}
