// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the Scaleform heap's aligned best fit from the bins (bins and free-block layout in
// scaleform_heap.h).
//
// engine_scaleform_heap_bins_take_best_aligned_0233d8d0 (0x0233d8d0) takes the bins, a length in
// units and an alignment mask (the alignment less 1), and takes out of its bin the first free block
// that holds the length from its first aligned address: from the first non-empty bin at or above
// the length's own, bin by bin, each walked from its head backwards (through the previous links).
// Null when none does.
#include <cstdint>

#include "scaleform_heap.h"

using namespace engine::scaleform_heap;

namespace {

bool holds_aligned(const FreeBlock *block, uint64_t units, uint64_t alignment_mask)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    const uintptr_t aligned = (address + alignment_mask) & ~alignment_mask;
    return aligned + units * unit_bytes <= address + units_of(block) * unit_bytes;
}

}  // namespace

extern "C" FreeBlock *bb_engine_scaleform_heap_bins_take_best_aligned_0233d8d0(Bins *bins, uint64_t units,
                                                                               uint64_t alignment_mask)
{
    const uint64_t first = bin_of(units);
    const uint64_t candidates = bins->mask >> first;
    if (!candidates)
        return nullptr;
    for (uint64_t bin = first + lowest_bin(candidates); bin < bin_count; ++bin) {
        FreeBlock *head = bins->heads[bin];
        if (!head)
            continue;
        FreeBlock *block = head;
        do {
            if (holds_aligned(block, units, alignment_mask)) {
                unlink_from_bin(bins, block, bin);
                return block;
            }
            block = link_block(block->prev_word);
        } while (block != head);
    }
    return nullptr;
}
