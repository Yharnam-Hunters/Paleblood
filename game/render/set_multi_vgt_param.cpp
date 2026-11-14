// SPDX-License-Identifier: GPL-2.0-or-later
// Write the primitive group size byte and PARTIAL_VS_WAVE_ON flag.
// This writer consumes eight size bits even though the hardware field is wider.
#include "present_register_packets.h"

namespace {
constexpr uint32_t ia_multi_vgt_param_index = 0x2aa;
constexpr unsigned partial_vs_wave_on_shift = 16;
constexpr uint32_t register_packet_words =
    sizeof(render::PreparationCommandPacket) / render::packet_word_bytes;
}  // namespace

extern "C" void bb_render_set_multi_vgt_param(render::PacketBuffer *commands,
                                            uint32_t primitive_group_size,
                                            uint32_t partial_vs_wave_on)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, register_packet_words, cursor)) return;

    const uint32_t value = static_cast<uint8_t>(primitive_group_size) |
                           ((partial_vs_wave_on & 1) << partial_vs_wave_on_shift);
    render::write_present_register_packet(commands, cursor, ia_multi_vgt_param_index, value);
}
