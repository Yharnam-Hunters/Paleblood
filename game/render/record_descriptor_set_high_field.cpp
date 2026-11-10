// SPDX-License-Identifier: GPL-2.0-or-later
// Store the top nibble while returning the lower bits of the old word.
#include "record_descriptor.h"

extern "C" uint32_t bb_render_record_descriptor_set_high_field(
    render::RecordDescriptorPrefix *descriptor, uint32_t value)
{
    constexpr uint32_t preserve = 0x0fffffff;
    constexpr unsigned field_shift = 28;
    const uint32_t old_bits = descriptor->pair_08.high.get() & preserve;
    descriptor->pair_08.high.set(old_bits | (value << field_shift));
    return old_bits;
}
