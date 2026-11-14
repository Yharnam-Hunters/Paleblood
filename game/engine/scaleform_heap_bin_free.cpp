// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's small-block free (pages, bins and free-block layout in
// scaleform_heap.h).
//
// engine_scaleform_heap_bin_free_0233ab20 (0x0233ab20) takes the engine's bins, the block's page,
// the block, where to describe the page and where to store the block's length in bytes. The length
// comes from the page's bitset, whose ends of the block become free. A free unit on either side
// hands the block to the merge (an original, reached by a tail jump, which joins the neighbours
// and bins the result); otherwise the block goes to the head of its bin as it is.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

// Joins a freed block with its free neighbours (the flags say which) and bins the result.
RT_ORIGINAL(0x0233dc60, bins_merge_free,
            void(Bins *bins, uintptr_t block, uint64_t bytes, bool previous_free, bool next_free, Page *page));

}  // namespace

extern "C" void bb_engine_scaleform_heap_bin_free_0233ab20(Bins *bins, Page *page, uintptr_t block, PageView *view,
                                                           uint64_t *freed_bytes)
{
    *view = describe_page(page);
    const uint64_t unit = unit_in_page(*view, block);
    const uint64_t units = bitset_allocated_units(view->bitset, unit);
    const uint64_t bytes = units * unit_bytes;
    *freed_bytes = bytes;
    clear_free_ends(*view, block, units);

    // The page's first unit has no neighbour below; the units past the page's end are not its own.
    const bool previous_free = unit != 0 && unit_is_free(view->bitset, unit - 1);
    const bool next_free = block + bytes < view->end && unit_is_free(view->bitset, unit + units);
    if (previous_free || next_free) {
        bins_merge_free(bins, block, bytes, previous_free, next_free, page);
        return;
    }
    release_range(bins, *view, block, bytes);
}
