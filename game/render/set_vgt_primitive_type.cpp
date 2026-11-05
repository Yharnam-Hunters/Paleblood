// SPDX-License-Identifier: GPL-2.0-or-later
// Write the low six primitive-type bits to VGT_PRIMITIVE_TYPE.
#include "present_command_controls.h"

extern "C" void bb_render_set_vgt_primitive_type(render::PacketBuffer *commands,
                                                     uint32_t primitive_type)
{
    constexpr uint32_t required_words = 3;
    constexpr uint32_t set_uconfig_reg_one_value_header = 0xc0017900;
    constexpr uint32_t primitive_type_index = 0x242;
    constexpr uint32_t primitive_type_mask = 0x3f;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    auto *packet = reinterpret_cast<render::PreparationCommandPacket *>(cursor);
    packet->header.set(set_uconfig_reg_one_value_header);
    packet->register_index.set(primitive_type_index);
    packet->payload.set(primitive_type & primitive_type_mask);
    render::set_packet_cursor(commands, cursor + sizeof(render::PreparationCommandPacket));
}
