// SPDX-License-Identifier: GPL-2.0-or-later
// Presentation parameter pairs use captured command cursors and ordered DWORD stores.
// Float values are packet payload bits, without arithmetic or NaN/zero normalization.
#pragma once

#include "command_reservation.h"

#include "present_command_configuration.h"
#include "present_dirty_preparation_packets.h"

namespace render {
struct ParameterFloatPairPacket {
    PreparationCommandPacket second_input;
    PreparationCommandPacket first_input;
};
static_assert(offsetof(ParameterFloatPairPacket, second_input) == 0x00);
static_assert(offsetof(ParameterFloatPairPacket, first_input) == 0x0c);
static_assert(sizeof(ParameterFloatPairPacket) == 0x18);

inline uint32_t parameter_float_bits(const float &value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline void write_parameter_float_pair(PacketBuffer *commands, uintptr_t cursor,
                                       uint32_t second_index, uint32_t second_bits,
                                       uint32_t first_index, uint32_t first_bits)
{
    auto *packet = reinterpret_cast<ParameterFloatPairPacket *>(cursor);
    packet->second_input.header.set(preparation_packet::set_context_reg_one_value_header);
    packet->second_input.register_index.set(second_index);
    packet->second_input.payload.set(second_bits);
    packet->first_input.header.set(preparation_packet::set_context_reg_one_value_header);
    packet->first_input.register_index.set(first_index);
    packet->first_input.payload.set(first_bits);
    // The native publishes only after all six stores, from the captured cursor even
    // when those stores overlap the command object's cursor word.
    set_packet_cursor(commands, cursor + sizeof(ParameterFloatPairPacket));
}
}  // namespace render
