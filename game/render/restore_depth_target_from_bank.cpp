// SPDX-License-Identifier: GPL-2.0-or-later
// Restore the depth target from the bank selected by flag bit 1.
// Its bank selection is independent of the color-target bank in flag bit 0.
#include "present_preparation_restore.h"

extern "C" void bb_render_restore_depth_target_from_bank(
    render::TargetRestoreState *state, render::PacketBuffer *commands)
{
    using namespace render;
    const uint32_t flags = state->bank_flags.get();
    const uint32_t bank = (flags >> depth_target_bank_bit) & 1;
    const uintptr_t candidate = state->banks[bank].depth_target.get();
    render_set_depth_render_target(commands, target_descriptor_payload(candidate));
}
