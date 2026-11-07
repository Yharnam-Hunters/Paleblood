// SPDX-License-Identifier: GPL-2.0-or-later
// Clear only bit 14, then OR the full input byte at that position. Neighboring
// bits retain their old values unless the shifted input sets them.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_second_auxiliary_flag(render::RecordConstructorPrefix *record,
                                                              uint32_t value_input)
{
    // The native entry consumes SIL; incoming upper bits need explicit truncation.
    const uint32_t value = static_cast<uint8_t>(value_input);
    constexpr uint32_t preserve = 0xffffbfff, byte_shift = 14;
    const uint32_t old_bits = record->pair_10.low.get() & preserve;
    record->pair_10.low.set(old_bits | (uint32_t(value) << byte_shift));
    return old_bits;
}
