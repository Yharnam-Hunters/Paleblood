// SPDX-License-Identifier: GPL-2.0-or-later
// Write VGT_REUSE_OFF through the GPU library's single-register packet path.
// The original flips bit zero of the input byte, including noncanonical values.
#include "present_register_packets.h"

extern "C" void bb_render_set_vertex_reuse(render::PacketBuffer *commands, uint32_t enabled)
{
    constexpr uint32_t vgt_reuse_off_index = 0x2ad;
    constexpr uint32_t reuse_off_bit = 1;
    uintptr_t cursor;
    if (!render::reserve_words(commands, render::preparation_packet::words, cursor)) return;

    const uint32_t value = static_cast<uint8_t>(enabled) ^ reuse_off_bit;
    render::write_present_register_packet(commands, cursor, vgt_reuse_off_index, value);
}
