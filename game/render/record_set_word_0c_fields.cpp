// SPDX-License-Identifier: GPL-2.0-or-later
// Replace the low 11 and shifted 11 fields. Return the preserved old-word bits.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_word_0c_fields(render::RecordConstructorPrefix *record,
                                                       uint32_t first, uint32_t second)
{
    constexpr uint32_t preserve = 0xff001800, first_mask = 0x7ff;
    constexpr uint32_t second_mask = 0x00ffe000, second_shift = 13;
    const uint32_t old_bits = record->sizing.multiplier.get() & preserve;
    record->sizing.multiplier.set(old_bits | (first & first_mask) | ((second << second_shift) & second_mask));
    return old_bits;
}
