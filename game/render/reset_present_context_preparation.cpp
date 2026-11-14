// SPDX-License-Identifier: GPL-2.0-or-later
// Invalidate both nine-entry banks and reset the surrounding preparation state.
// Preserve the low two bank-selection flags and the shared scalar bit pattern.
#include <limits>

#include "present_context_reset.h"

namespace {
constexpr uint32_t invalid_word = std::numeric_limits<uint32_t>::max();
constexpr uint64_t invalid_double_word = std::numeric_limits<uint64_t>::max();
constexpr uint32_t preparation_slot_count = 8;
constexpr uint32_t scalar_word_count = 4;
constexpr uint32_t scalar_one_bits = 0x3f800000;
constexpr uint16_t packed_state_value = 0x1c00;
constexpr uint32_t preserved_flag_mask = 0x3;
constexpr uint32_t reset_flag_bits = 0xffc;
}

extern "C" void bb_render_reset_present_context_preparation(
    render::ContextPreparationReset *state)
{
    state->invalid_00.set(invalid_word);
    state->invalid_08.set(invalid_double_word);
    for (uint32_t i = 0; i < preparation_slot_count; ++i) {
        state->slot_indices[i].set(invalid_word);
        state->banks[0][i].set(invalid_double_word);
        state->banks[1][i].set(invalid_double_word);
        state->slot_states[i].set(0);
    }
    const uint32_t scalar = render::context_reset_scalar_word.get();
    for (uint32_t i = 0; i < scalar_word_count; ++i) state->scalar_words[i].set(scalar);
    state->state_40.set(0);
    state->invalid_48.set(invalid_word);
    state->invalid_4c.set(invalid_word);
    state->state_50.set(0);
    state->invalid_58.set(invalid_double_word);
    state->banks[0][preparation_slot_count].set(invalid_double_word);
    state->banks[1][preparation_slot_count].set(invalid_double_word);
    state->invalid_f0.set(invalid_word);
    state->invalid_f4.set(invalid_word);
    state->invalid_190.set(invalid_double_word);
    state->invalid_198.set(invalid_double_word);
    state->state_fe.set(0);
    state->state_ff.set(0);
    state->scalar_140.set(scalar_one_bits);
    state->state_144.set(0);
    state->packed_146.set(packed_state_value);
    const uint32_t flags = (state->bank_flags.get() & preserved_flag_mask) | reset_flag_bits;
    state->bank_flags.set(flags);
}
