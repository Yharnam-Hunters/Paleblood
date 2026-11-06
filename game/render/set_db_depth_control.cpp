// SPDX-License-Identifier: GPL-2.0-or-later
// Write the observed mask of DB_DEPTH_CONTROL depth/stencil test and write-control fields.
#include "present_register_packets.h"

extern "C" void bb_render_set_db_depth_control(render::PacketBuffer *commands,
                                                  uint32_t value)
{
    constexpr uint32_t depth_control_index = 0x200;
    constexpr uint32_t value_mask = 0xc07007ff;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    render::write_present_register_packet(commands, cursor, depth_control_index, value & value_mask);
}
