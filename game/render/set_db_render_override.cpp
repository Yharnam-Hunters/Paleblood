// SPDX-License-Identifier: GPL-2.0-or-later
// Write DB_RENDER_OVERRIDE with the subset of input bits retained by this wrapper.
// The mask describes observed behavior, rather than all hardware-valid register bits.
#include "present_register_packets.h"

extern "C" void bb_render_set_db_render_override(render::PacketBuffer *commands,
                                                   uint32_t control)
{
    constexpr uint32_t db_render_override_index = 3;
    constexpr uint32_t db_render_override_mask = 0xf8000040;
    uintptr_t cursor;
    if (!render::reserve_words(commands, render::preparation_packet::words, cursor)) return;

    const uint32_t value = control & db_render_override_mask;
    render::write_present_register_packet(commands, cursor, db_render_override_index, value);
}
