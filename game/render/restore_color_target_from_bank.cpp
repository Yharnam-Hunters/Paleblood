// SPDX-License-Identifier: GPL-2.0-or-later
// Restore one color target from the bank selected by flag bit 0.
// Keep the preparation cache unchanged while delegating the register writes.
#include "present_preparation_restore.h"

extern "C" void bb_render_restore_color_target_from_bank(
    render::TargetRestoreState *state, render::PacketBuffer *commands, uint32_t slot)
{
    using namespace render;
    const uint32_t flags = state->bank_flags.get();
    const uint32_t bank = (flags >> color_target_bank_bit) & 1;
    const uintptr_t candidate = color_target_source(state, bank, slot);
    render_set_render_target(commands, slot, target_descriptor_payload(candidate));
}
