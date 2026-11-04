// SPDX-License-Identifier: GPL-2.0-or-later
// Clear only bit 13, then OR all eight input bits beginning there. Higher input
// bits can set neighboring fields, so this input must not become a bool.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_first_auxiliary_flag(render::RecordConstructorPrefix *record,
                                                              uint32_t value_input)
{
    // The native entry consumes SIL; incoming upper bits need explicit truncation.
    const uint32_t value = static_cast<uint8_t>(value_input);
    constexpr uint32_t preserve = 0xffffdfff, byte_shift = 13;
    const uint32_t old_bits = record->pair_10.low.get() & preserve;
    record->pair_10.low.set(old_bits | (uint32_t(value) << byte_shift));
    return old_bits;
}
