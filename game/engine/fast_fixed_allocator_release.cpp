// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: giving a fast fixed-size allocator's chunks back (Dantelion2 DLFastFixedAllocator;
// layout in fast_fixed_allocator.h).
//
// engine_fast_fixed_allocator_release_02a3bc40 (0x02a3bc40) frees every chunk of both chunk lists,
// first list first, each in list order, then the spare chunk when there is one, all to the
// backing allocator. It is the allocator's destructor body: the fast small object allocators run
// it for each size class when they are destroyed. The lists and fields are left as they were.
#include "fast_fixed_allocator.h"

using namespace engine::fast_fixed_allocator;

extern "C" void bb_engine_fast_fixed_allocator_release_02a3bc40(Allocator *allocator)
{
    engine::fixed_allocator::BackingAllocator *backing = allocator->backing;
    for (ListLinks &list : allocator->chunk_lists) {
        ListLinks *links = list.next;
        while (links != &list) {
            ListLinks *next = links->next;
            backing->vtable->free(backing, chunk_memory(allocator, links));
            links = next;
        }
    }
    if (allocator->spare_chunk)
        backing->vtable->free(backing, chunk_memory(allocator, allocator->spare_chunk));
}
