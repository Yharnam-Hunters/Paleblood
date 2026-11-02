// SPDX-License-Identifier: GPL-2.0-or-later
// Replace this word's low22 bits; the native return is its preserved old bits.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_word_28_low22(render::RecordConstructorPrefix *record, uint32_t value)
{
    constexpr uint32_t preserve = 0xffc00000, field_mask = 0x003fffff;
    const uint32_t old_bits = record->tail_28.get() & preserve;
    record->tail_28.set(old_bits | (value & field_mask));
    return old_bits;
}
