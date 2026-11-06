// SPDX-License-Identifier: GPL-2.0-or-later
// Write the complete CB_TARGET_MASK value, preserving all supplied component-enable bits.
#include "present_register_packets.h"

extern "C" void bb_render_set_cb_target_mask(render::PacketBuffer *commands, uint32_t value)
{
    constexpr uint32_t color_target_mask_index = 0x8e;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    render::write_present_register_packet(commands, cursor, color_target_mask_index, value);
}
