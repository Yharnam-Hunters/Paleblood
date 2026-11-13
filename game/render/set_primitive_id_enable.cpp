// SPDX-License-Identifier: GPL-2.0-or-later
// Write VGT_PRIMITIVEID_EN with the original's complete byte payload.
// Noncanonical enable bytes are preserved rather than normalized to a boolean.
#include "present_register_packets.h"

extern "C" void bb_render_set_primitive_id_enable(render::PacketBuffer *commands,
                                                    uint32_t enabled)
{
    constexpr uint32_t vgt_primitiveid_en_index = 0x2a1;
    uintptr_t cursor;
    if (!render::reserve_words(commands, render::preparation_packet::words, cursor)) return;

    const uint32_t value = static_cast<uint8_t>(enabled);
    render::write_present_register_packet(commands, cursor, vgt_primitiveid_en_index, value);
}
