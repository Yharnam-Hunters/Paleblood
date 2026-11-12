// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's small-block allocation from the bins (pages, bins and free-block
// layout in scaleform_heap.h).
//
// engine_scaleform_heap_bin_allocate_0233a7f0 (0x0233a7f0) takes the engine's bins, a request in
// bytes (the heap's allocation rounds it to 16) and where to describe the block's page; about
// 1M calls in a boot to character creation. The bins' best fit (an original that takes the block
// out of its bin) gives a block or nothing; the rest of the block beyond the request goes back to a
// bin as a free block, and the request is marked allocated in the page's bitset. The caller counts
// the block in the page's headers.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

// The free block of at least `units` units that fits best, taken out of its bin; null when none.
RT_ORIGINAL(0x0233d750, bins_take_best, FreeBlock *(Bins *bins, uint64_t units));

}  // namespace

extern "C" FreeBlock *bb_engine_scaleform_heap_bin_allocate_0233a7f0(Bins *bins, uint64_t request, PageView *view)
{
    FreeBlock *block = bins_take_best(bins, request >> unit_shift);
    if (!block)
        return nullptr;
    *view = describe_page(page_of_free_block(block));
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    const uint64_t rest = units_of(block) * unit_bytes - request;
    if (rest != 0) {
        release_range(bins, *view, address + request, rest);
        clear_free_ends(*view, address + request, rest >> unit_shift);
    }
    bitset_mark_allocated(view->bitset, unit_in_page(*view, address), request >> unit_shift);
    return block;
}
