// SPDX-License-Identifier: GPL-2.0-or-later
// Write the low 24 bits of DB_STENCIL_CONTROL, including front- and back-face stencil operations.
#include "present_register_packets.h"
#include "render_observe.h"

extern "C" void bb_render_set_db_stencil_control(render::PacketBuffer *commands,
                                                  uint32_t value)
{
    constexpr uint32_t stencil_control_index = 0x10b;
    constexpr uint32_t value_mask = 0x00ffffff;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    render::observe_context_register(commands, cursor, stencil_control_index, value & value_mask);
    render::write_present_register_packet(commands, cursor, stencil_control_index, value & value_mask);
}
