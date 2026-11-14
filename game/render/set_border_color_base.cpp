// SPDX-License-Identifier: GPL-2.0-or-later
// Encode address bits 8-39 in TA_BC_BASE_ADDR for the graphics border-color table.
// The original discards the low address byte even when it is nonzero.
#include "present_register_packets.h"

extern "C" void bb_render_set_border_color_base(render::PacketBuffer *commands,
                                                  uint64_t gpu_address)
{
    constexpr uint32_t ta_bc_base_addr_index = 0x20;
    constexpr uint32_t border_color_address_shift = 8;
    uintptr_t cursor;
    if (!render::reserve_words(commands, render::preparation_packet::words, cursor)) return;

    const uint32_t value = static_cast<uint32_t>(gpu_address >> border_color_address_shift);
    render::write_present_register_packet(commands, cursor, ta_bc_base_addr_index, value);
}
