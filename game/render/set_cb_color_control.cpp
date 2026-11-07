// SPDX-License-Identifier: GPL-2.0-or-later
// Write CB_COLOR_CONTROL.MODE and ROP3 from their masked input fields.
#include "present_register_packets.h"
#include "render_observe.h"

extern "C" void bb_render_set_cb_color_control(render::PacketBuffer *commands,
                                                  uint32_t color_mode, uint32_t rop3)
{
    constexpr uint32_t color_control_index = 0x202;
    constexpr uint32_t color_mode_shift = 4;
    constexpr uint32_t color_mode_mask = 0x70;
    constexpr uint32_t rop3_shift = 16;
    constexpr uint32_t rop3_mask = 0x00ff0000;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    const uint32_t packed = ((rop3 << rop3_shift) & rop3_mask) |
                            ((color_mode << color_mode_shift) & color_mode_mask);
    render::observe_context_register(commands, cursor, color_control_index, packed);
    render::write_present_register_packet(commands, cursor, color_control_index, packed);
}
