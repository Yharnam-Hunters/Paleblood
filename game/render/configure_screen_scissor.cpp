// SPDX-License-Identifier: GPL-2.0-or-later
// Write PA_SC_SCREEN_SCISSOR_TL/BR using the low 16 bits of each supplied coordinate.
// Packet stores may overlap the cursor; its final increment reads the stored value after those
// writes.
#include "present_parameter_transforms.h"
#include "command_reservation.h"

extern "C" void bb_render_configure_screen_scissor(render::PacketBuffer *commands,
                                                   uint32_t x, uint32_t y,
                                                   uint32_t end_x, uint32_t end_y)
{
    using namespace render;
    constexpr uint32_t required_words = 4;
    constexpr uint32_t set_context_reg_two_values_header = 0xc0026900;
    constexpr uint32_t screen_scissor_top_left_index = 0x0c;
    constexpr uint32_t halfword_mask = 0xffff;
    constexpr uint32_t halfword_shift = 16;
    uintptr_t cursor;
    if (!reserve_words(commands, required_words, cursor)) return;
    const uint32_t first = (x & halfword_mask) | (y << halfword_shift);
    const uint32_t second = (end_x & halfword_mask) | (end_y << halfword_shift);
    auto *packet = reinterpret_cast<RecordParameterPairPacket *>(cursor);
    packet->header.set(set_context_reg_two_values_header);
    packet->index.set(screen_scissor_top_left_index);
    packet->first.set(first);
    packet->second.set(second);
    set_packet_cursor(commands, packet_cursor(commands) + sizeof(RecordParameterPairPacket));
}
