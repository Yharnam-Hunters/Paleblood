// SPDX-License-Identifier: GPL-2.0-or-later
// Write vertical then horizontal guard-band clip adjustments as unchanged float bit patterns.
// Capture both float representations before the capacity callback and publish from the captured
// cursor.
#include "present_parameter_pairs.h"

extern "C" void bb_render_set_guard_band_clip_adjust(render::PacketBuffer *commands,
                                                       float horizontal_clip_adjustment, float vertical_clip_adjustment)
{
    constexpr uint32_t guard_band_vertical_clip_index = 0x2fa;
    constexpr uint32_t guard_band_horizontal_clip_index = 0x2fc;
    const uint32_t second_bits = render::parameter_float_bits(vertical_clip_adjustment);
    const uint32_t first_bits = render::parameter_float_bits(horizontal_clip_adjustment);
    constexpr uint32_t required_words = 6;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    render::write_parameter_float_pair(commands, cursor, guard_band_vertical_clip_index, second_bits,
                                        guard_band_horizontal_clip_index, first_bits);
}
