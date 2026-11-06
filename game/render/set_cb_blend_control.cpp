// SPDX-License-Identifier: GPL-2.0-or-later
// Write masked CB_BLENDn_CONTROL for the selected color-buffer bank.
#include "present_register_packets.h"

extern "C" void bb_render_set_cb_blend_control(render::PacketBuffer *commands,
                                                  uint32_t color_buffer_index, uint32_t value)
{
    constexpr uint32_t blend_control_base_index = 0x1e0;
    constexpr uint32_t value_mask = 0xffff1fff;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    render::write_present_register_packet(commands, cursor, color_buffer_index + blend_control_base_index, value & value_mask);
}
