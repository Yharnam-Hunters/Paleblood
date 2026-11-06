// SPDX-License-Identifier: GPL-2.0-or-later
// Preserve the native byte-wide input: doubling it can contribute bits beyond bit one.
#include "present_register_state.h"

extern "C" uint32_t bb_render_set_record_register_enabled(render::RecordRegisterState *state,
                                                          uint32_t enabled)
{
    using namespace render;
    auto *word = record_register_word(state);
    const uint32_t preserved = word->get() & record_register_enabled_preserve;
    const uint32_t input = static_cast<uint8_t>(enabled);
    word->set(preserved | (input + input));
    return preserved;
}
