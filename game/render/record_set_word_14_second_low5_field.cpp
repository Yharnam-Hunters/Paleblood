// SPDX-License-Identifier: GPL-2.0-or-later
// Replace the second five-bit field while retaining the rest of the word.
#include "record_fields.h"

extern "C" uint32_t bb_render_record_set_word_14_second_low5_field(render::RecordConstructorPrefix *record,
                                                                 uint32_t value)
{
    constexpr uint32_t preserve = 0xfffffc1f, field_mask = 31, field_shift = 5;
    const uint32_t old_bits = record->pair_10.high.get() & preserve;
    record->pair_10.high.set(old_bits | ((value & field_mask) << field_shift));
    return old_bits;
}
