// SPDX-License-Identifier: GPL-2.0-or-later
// The upper word partition decides whether the shared four-bit count belongs
// to the level path. Capture the source word once before that decision.
#include "record_descriptor_decoder.h"

extern "C" uint32_t bb_render_record_descriptor_level_count(const render::RecordDescriptorPrefix *record)
{
    constexpr uint32_t last_level_partition = 0xdfffffff, count_mask = 15;
    constexpr unsigned count_shift = 16;
    const uint32_t word = record->pair_08.high.get();
    return word > last_level_partition ? 0 : (word >> count_shift) & count_mask;
}
