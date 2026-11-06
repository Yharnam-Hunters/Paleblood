// SPDX-License-Identifier: GPL-2.0-or-later
// Classify the encoding's first byte. The two accepted values produce distinct
// flags; this is not a Boolean-valued predicate even though its caller tests it.
#include "record_descriptor_decoder.h"

extern "C" uint32_t bb_render_record_encoding_byte_flags(const uint32_t *encoding)
{
    constexpr uint8_t first_encoding = 2, second_encoding = 4;
    constexpr uint32_t first_flags = 1, second_flags = 3;
    const uint8_t value = *reinterpret_cast<const uint8_t *>(encoding);
    if (value == second_encoding) return second_flags;
    return value == first_encoding ? first_flags : 0;
}
