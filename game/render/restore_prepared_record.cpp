// SPDX-License-Identifier: GPL-2.0-or-later
// Restore one selected record binding without changing the preparation cache itself.
// The observed consumer ignores the delegated native return, so this boundary is void.
#include "present_preparation_restore.h"

extern "C" void bb_render_restore_prepared_record_from_bank(render::RecordPreparationState *preparation,
                                                render::PacketBuffer *commands, uint32_t slot)
{
    using namespace render;
    auto *state = reinterpret_cast<PreparationRestoreState *>(preparation);
    const uint32_t flags = state->bank_flags.get();
    const uint32_t bank = (flags >> preparation_record_bank_bit) & 1;
    const uintptr_t candidate = preparation_record_source(state, bank, slot);
    emit_prepared_record_packet(commands, slot, preparation_payload(candidate));
}
