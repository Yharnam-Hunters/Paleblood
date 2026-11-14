// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's merge of a freed block with its free neighbours (bins and free-block
// layout in scaleform_heap.h).
//
// engine_scaleform_heap_bins_merge_free_0233dc60 (0x0233dc60) takes the bins, the freed block, its
// length in bytes, whether the units before and after it are free (the free found them so in the
// page's bitset) and the page. The block first gets its free length; a free block before it (found
// through that block's tail copy, just below) and the one after it (found through the freed block's
// own length) leave their bins, and the joined block goes to the head of the bin of the joined
// length, with the page pointer when it is long enough.
#include <cstdint>

#include "scaleform_heap.h"

using namespace engine::scaleform_heap;

extern "C" void bb_engine_scaleform_heap_bins_merge_free_0233dc60(Bins *bins, uintptr_t block, uint64_t bytes,
                                                                  bool previous_free, bool next_free, Page *page)
{
    auto *freed = reinterpret_cast<FreeBlock *>(block);
    uint64_t units = bytes >> unit_shift;
    write_free_units(freed, units);
    FreeBlock *merged = freed;
    if (previous_free) {
        const auto *previous_tail = reinterpret_cast<const FreeBlock *>(block - header_bytes);
        merged = reinterpret_cast<FreeBlock *>(block - units_of(previous_tail) * unit_bytes);
        units += units_of(merged);
        unlink_free(bins, merged);
    }
    if (next_free) {
        auto *next = reinterpret_cast<FreeBlock *>(block + units_of(freed) * unit_bytes);
        units += units_of(next);
        unlink_free(bins, next);
    }
    write_free_units(merged, units);
    if (units_of(merged) >= units_with_page)
        merged->page = page;
    push_to_bin(bins, merged, bin_of(units));
}
