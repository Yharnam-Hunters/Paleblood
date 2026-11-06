// SPDX-License-Identifier: GPL-2.0-or-later
// Write the observed masked X/Y fields of PA_SU_HARDWARE_SCREEN_OFFSET.
#include "present_register_packets.h"

extern "C" void bb_render_set_screen_offset(render::PacketBuffer *commands,
                                                         uint32_t x_offset, uint32_t y_offset)
{
    constexpr uint32_t hardware_screen_offset_index = 0x8d;
    constexpr uint32_t x_offset_mask = 0x1fc;
    constexpr uint32_t y_offset_shift = 16;
    constexpr uint32_t y_offset_mask = 0x01fc0000;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    const uint32_t packed = (x_offset & x_offset_mask) | ((y_offset << y_offset_shift) & y_offset_mask);
    render::write_present_register_packet(commands, cursor, hardware_screen_offset_index, packed);
}
