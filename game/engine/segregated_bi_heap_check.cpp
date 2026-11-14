// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the segregated bi-heap's integrity check (Dantelion2 DLSegregatedBiHeap; descriptors and
// lists in segregated_bi_heap.h).
//
// engine_segregated_bi_heap_is_consistent_02488cf0 (0x02488cf0) takes a heap and returns whether
// its bookkeeping holds together; the frees call it to choose between their error messages when an
// address is in neither allocation chain. A heap that is not set up passes. Otherwise the range
// must be non-empty and non-null, the terminator's four links set and its address the range's
// limit. No list may loop: the two allocation chains, the address chain in both directions and the
// free list in both directions are each walked with a slow and a fast pointer. Every block in an
// allocation chain must have a null list_prev. Walking the blocks in address order, each must be
// linked back by its neighbours and start no lower than the one before and no higher than the one
// after (the terminator counts as the limit). Walking the free list, each block's neighbours must
// be free and link back to it, and the addresses must rise strictly.
#include <cstdint>

#include "segregated_bi_heap.h"

namespace {

using namespace engine::segregated_bi_heap;

using Link = Descriptor *Descriptor::*;

// Whether following `link` from `start` loops before reaching `end` (Floyd's cycle check, as the
// heap does it: the fast pointer moves two steps, the slow one one, and meeting means a loop).
bool loops(Descriptor *start, Descriptor *end, Link link)
{
    Descriptor *slow = start;
    Descriptor *fast = start;
    while (fast != end && fast->*link != end) {
        fast = (fast->*link)->*link;
        if (fast == end)
            return false;
        slow = slow->*link;
        if (fast == slow)
            return true;
    }
    return false;
}

bool chain_is_in_use(const Descriptor *head)
{
    for (const Descriptor *block = head; block; block = block->list_next) {
        if (block->list_prev)
            return false;
    }
    return true;
}

bool address_chain_is_ordered(Descriptor *terminator)
{
    for (Descriptor *block = terminator->geo_next; block != terminator;) {
        Descriptor *next = block->geo_next;
        if (next->geo_prev != block)
            return false;
        Descriptor *prev = block->geo_prev;
        if (prev->geo_next != block)
            return false;
        if (prev != terminator && prev->address > block->address)
            return false;
        if (block->address > next->address)
            return false;
        block = next;
    }
    return true;
}

bool free_list_is_ordered(Descriptor *terminator)
{
    for (Descriptor *block = terminator->list_next; block != terminator;) {
        Descriptor *prev = block->list_prev;
        if (!prev || !prev->list_prev)
            return false;
        Descriptor *next = block->list_next;
        if (!next->list_prev || next->list_prev != block)
            return false;
        if (prev->list_next != block)
            return false;
        if (prev != terminator && prev->address >= block->address)
            return false;
        if (block->address >= next->address)
            return false;
        block = next;
    }
    return true;
}

}  // namespace

extern "C" bool bb_engine_segregated_bi_heap_is_consistent_02488cf0(Heap *heap)
{
    if (!heap->initialized)
        return true;
    if (heap->lowest >= heap->limit || heap->lowest == 0 || heap->limit == 0)
        return false;
    Descriptor *terminator = &heap->terminator;
    if (!terminator->geo_prev || !terminator->geo_next || !terminator->list_prev || !terminator->list_next)
        return false;
    if (terminator->address != heap->limit)
        return false;

    if (loops(heap->bottom_allocations, nullptr, &Descriptor::list_next) ||
        loops(heap->top_allocations, nullptr, &Descriptor::list_next) ||
        loops(terminator->geo_next, terminator, &Descriptor::geo_next) ||
        loops(terminator->geo_prev, terminator, &Descriptor::geo_prev) ||
        loops(terminator->list_next, terminator, &Descriptor::list_next) ||
        loops(terminator->list_prev, terminator, &Descriptor::list_prev))
        return false;
    if (!chain_is_in_use(heap->bottom_allocations) || !chain_is_in_use(heap->top_allocations))
        return false;
    return address_chain_is_ordered(terminator) && free_list_is_ordered(terminator);
}
