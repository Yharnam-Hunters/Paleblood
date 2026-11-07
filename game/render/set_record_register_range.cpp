// SPDX-License-Identifier: GPL-2.0-or-later
// Pack one bit from the first input and three from the last, preserving all other bits.
#include "present_register_state.h"

extern "C" uint32_t bb_render_set_record_register_range(render::RecordRegisterState *state,
                                                        uint32_t first, uint32_t last)
{
    using namespace render;
    constexpr unsigned first_shift = 2;
    constexpr uint32_t first_mask = 0x04;
    constexpr unsigned last_shift = 4;
    constexpr uint32_t last_mask = 0x70;
    auto *word = record_register_word(state);
    const uint32_t preserved = word->get() & record_register_range_preserve;
    const uint32_t first_field = (first << first_shift) & first_mask;
    const uint32_t last_field = (last << last_shift) & last_mask;
    word->set(preserved | first_field | last_field);
    return preserved;
}
