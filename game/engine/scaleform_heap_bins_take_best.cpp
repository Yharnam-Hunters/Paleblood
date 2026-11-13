// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's best fit from the bins (bins and free-block layout in
// scaleform_heap.h).
//
// engine_scaleform_heap_bins_take_best_0233d750 (0x0233d750) takes the bins and a length in units,
// and takes the head of the first non-empty bin from the length's own bin up out of its bin (null
// when there is none). A bin below the last holds blocks of exactly its length, so the block always
// fits; the last bin holds every longer length, and its head is taken without a check (the
// allocations never ask for more than 32 units).
#include <cstdint>

#include "scaleform_heap.h"

using namespace engine::scaleform_heap;

extern "C" FreeBlock *bb_engine_scaleform_heap_bins_take_best_0233d750(Bins *bins, uint64_t units)
{
    const uint64_t first = bin_of(units);
    const uint64_t candidates = bins->mask >> first;
    if (!candidates)
        return nullptr;
    const uint64_t bin = first + lowest_bin(candidates);
    FreeBlock *block = bins->heads[bin];
    unlink_from_bin(bins, block, bin);
    return block;
}
