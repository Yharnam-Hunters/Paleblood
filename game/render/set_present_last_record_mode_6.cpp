// SPDX-License-Identifier: GPL-2.0-or-later
// Replace one packed mode bit using only the input's low bit. The caller emits
// the resulting word; the native return retains the old word with this bit clear.
#include "present_last_record_fields.h"

namespace {
constexpr uint32_t mode_bit_shift = 6;
constexpr uint32_t mode_bit_mask = uint32_t(1) << mode_bit_shift;
}

extern "C" uint64_t bb_render_set_present_last_record_mode_6(
    render::LastRecordModeWord *mode, uint32_t value)
{
    const uint32_t preserved = mode->word.get() & ~mode_bit_mask;
    mode->word.set(preserved | ((value << mode_bit_shift) & mode_bit_mask));
    return uint64_t(preserved);
}
