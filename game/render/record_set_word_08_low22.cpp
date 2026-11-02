// SPDX-License-Identifier: GPL-2.0-or-later
// Replace the low22 bits and return the old upper bits.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_word_08_low22(render::RecordConstructorPrefix *record, uint32_t value)
{
    constexpr uint32_t preserve = 0xffc00000, field_mask = 0x003fffff;
    const uint32_t old_bits = record->pair_08.low.get() & preserve;
    record->pair_08.low.set(old_bits | (value & field_mask));
    return old_bits;
}
