// SPDX-License-Identifier: GPL-2.0-or-later
// Duplicate the supplied low11 bits into two fields; no component conversion.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_word_04_repeated_field(render::RecordConstructorPrefix *record,
                                                               uint32_t value)
{
    constexpr uint32_t preserve = 0x800ff800, field_mask = 0x7ff;
    constexpr uint32_t repeated_mask = 0x7ff00000, repeated_shift = 20;
    const uint32_t old_bits = record->pair_00.high.get() & preserve;
    record->pair_00.high.set(old_bits | (value & field_mask) | ((value << repeated_shift) & repeated_mask));
    return old_bits;
}
