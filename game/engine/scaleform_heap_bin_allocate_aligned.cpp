// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's aligned small-block allocation from the bins (pages, bins and
// free-block layout in scaleform_heap.h).
//
// engine_scaleform_heap_bin_allocate_aligned_0233ae40 (0x0233ae40) takes the engine's bins, a
// request in bytes, an alignment (a power of two) and where to describe the block's page. The bins'
// aligned best fit (an original that takes the block out of its bin) gives a block that holds the
// request at its first aligned address; the bytes before that address and those after the request
// go back to the bins as free blocks, and the request is marked allocated in the page's bitset.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

// The free block that fits `units` units at an address with no bits of `alignment_mask` set, taken
// out of its bin; null when none.
RT_ORIGINAL(0x0233d8d0, bins_take_best_aligned, FreeBlock *(Bins *bins, uint64_t units, uint64_t alignment_mask));

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_bin_allocate_aligned_0233ae40(Bins *bins, uint64_t request,
                                                                            uint64_t alignment, PageView *view)
{
    const uint64_t alignment_mask = alignment - 1;
    FreeBlock *block = bins_take_best_aligned(bins, request >> unit_shift, alignment_mask);
    if (!block)
        return 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    const uintptr_t aligned = (address + alignment_mask) & (0 - alignment);
    *view = describe_page(page_of_free_block(block));
    const uintptr_t block_end = address + units_of(block) * unit_bytes;

    if (aligned != address) {
        const uint64_t front = aligned - address;
        release_range(bins, *view, address, front);
        clear_free_ends(*view, address, front >> unit_shift);
    }
    const uintptr_t request_end = aligned + request;
    if (block_end != request_end) {
        const uint64_t rest = block_end - request_end;
        release_range(bins, *view, request_end, rest);
        clear_free_ends(*view, request_end, rest >> unit_shift);
    }
    bitset_mark_allocated(view->bitset, unit_in_page(*view, aligned), request >> unit_shift);
    return aligned;
}
