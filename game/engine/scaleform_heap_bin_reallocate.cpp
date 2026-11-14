// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's in-place small-block reallocation (pages, bins and free-block
// layout in scaleform_heap.h).
//
// engine_scaleform_heap_bin_reallocate_0233b2f0 (0x0233b2f0) takes the engine's bins, the block's
// page, the block, the new length in bytes (a multiple of 16), where to store the block's old
// length and where to describe the page. It returns the block when it now has the new length, or
// 0 when it cannot grow where it is (the heap then moves it). A shrinking block gives its end back,
// joined with a free block right after it; a growing one takes from the free block right after it,
// which must be long enough, and gives back what it does not need.
//
// "Right after" stops at the half of the page the block is in: below the boundary, the lower
// header (and the bitset, when it sits there) ends the usable space.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

// Where the usable space of the block's half of the page ends.
uintptr_t usable_end(const PageView &view, uintptr_t block)
{
    if (block >= view.boundary)
        return view.end;
    const uintptr_t bitset = reinterpret_cast<uintptr_t>(view.bitset);
    return bitset < view.boundary ? view.boundary - header_bytes - bitset_bytes : view.boundary - header_bytes;
}

// The free block's bytes (as the bitset reads them).
uint64_t free_bytes(const FreeBlock *block)
{
    return units_of(block) * unit_bytes;
}

}  // namespace

extern "C" uintptr_t bb_engine_scaleform_heap_bin_reallocate_0233b2f0(Bins *bins, Page *page, uintptr_t block,
                                                                      uint64_t new_bytes, uint64_t *old_bytes,
                                                                      PageView *view)
{
    *view = describe_page(page);
    const uintptr_t limit = usable_end(*view, block);
    const uint64_t unit = unit_in_page(*view, block);
    const uint64_t old_units = bitset_allocated_units(view->bitset, unit);
    const uint64_t bytes = old_units * unit_bytes;
    *old_bytes = bytes;
    const uintptr_t after = block + bytes;
    auto *next = reinterpret_cast<FreeBlock *>(after);
    const bool next_is_free = after < limit && unit_is_free(view->bitset, unit + old_units);

    if (bytes >= new_bytes) {
        if (bytes == new_bytes)
            return block;
        const uint64_t joined = next_is_free ? free_bytes(next) : 0;
        const uint64_t rest = bytes - new_bytes + joined;
        if (rest == 0)
            return block;
        if (joined != 0)
            unlink_free(bins, next);
        release_range(bins, *view, block + new_bytes, rest);
        bitset_mark_allocated(view->bitset, unit, new_bytes >> unit_shift);
        clear_free_ends(*view, block + new_bytes, rest >> unit_shift);
        return block;
    }

    if (!next_is_free)
        return 0;
    const uint64_t available = free_bytes(next) + bytes;
    if (available < new_bytes)
        return 0;
    unlink_free(bins, next);
    const uint64_t rest = available - new_bytes;
    if (rest != 0) {
        release_range(bins, *view, block + new_bytes, rest);
        clear_free_ends(*view, block + new_bytes, rest >> unit_shift);
    }
    bitset_mark_allocated(view->bitset, unit, new_bytes >> unit_shift);
    return block;
}
