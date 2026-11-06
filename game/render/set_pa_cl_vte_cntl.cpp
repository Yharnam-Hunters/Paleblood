// SPDX-License-Identifier: GPL-2.0-or-later
// Expand the toggled input byte into overlapping register fields. The native result
// is the shifted byte, independent of the preserved old bits or the final stored word.
#include "present_command_configuration.h"

extern "C" uint32_t bb_render_set_pa_cl_vte_cntl(
    render::RecordConfigurationRegister *state, uint32_t value)
{
    constexpr uint32_t preserved_mask = 0xfffffac0;
    constexpr uint32_t byte_toggle_mask = 1;
    constexpr uint32_t low_copy_count = 6;
    constexpr uint32_t result_shift = 8;
    constexpr uint32_t high_copy_shift = 10;
    constexpr uint32_t stored_toggle_mask = 0x100;
    const uint32_t preserved = state->word.get() & preserved_mask;
    // SIL is a byte value, not a normalized Boolean. Toggle only its low bit.
    const uint32_t toggled = static_cast<uint32_t>(static_cast<uint8_t>(value)) ^ byte_toggle_mask;
    uint32_t low_copies = 0;
    for (uint32_t shift = 0; shift < low_copy_count; ++shift) low_copies |= toggled << shift;
    const uint32_t result = toggled << result_shift;
    const uint32_t updated =
        (preserved | low_copies | result | (toggled << high_copy_shift)) ^ stored_toggle_mask;
    state->word.set(updated);
    return result;
}
