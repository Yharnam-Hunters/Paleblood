// SPDX-License-Identifier: GPL-2.0-or-later
// Read the same count bits only in the complementary upper-word partition.
#include "record_descriptor_decoder.h"

extern "C" uint32_t bb_render_record_descriptor_count_exponent(const render::RecordDescriptorPrefix *record)
{
    constexpr uint32_t first_exponent_partition = 0xe0000000, count_mask = 15;
    constexpr unsigned count_shift = 16;
    const uint32_t word = record->pair_08.high.get();
    return word < first_exponent_partition ? 0 : (word >> count_shift) & count_mask;
}
