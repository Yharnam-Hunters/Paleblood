// SPDX-License-Identifier: GPL-2.0-or-later
// Restore the selector from flag bit 1's bank, independently of the record bank bit 0.
// It forwards the full payload pointer and retains the observed ignored-return profile.
#include "present_preparation_restore.h"

extern "C" void bb_render_restore_prepared_selector_from_bank(render::RecordPreparationState *preparation,
                                                    render::PacketBuffer *commands)
{
    using namespace render;
    auto *state = reinterpret_cast<PreparationRestoreState *>(preparation);
    const uint32_t flags = state->bank_flags.get();
    const uint32_t bank = (flags >> preparation_selector_bank_bit) & 1;
    const uintptr_t candidate = state->banks[bank].selector.get();
    render_set_depth_render_target(commands, preparation_payload(candidate));
}
