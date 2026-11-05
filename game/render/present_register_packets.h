// SPDX-License-Identifier: GPL-2.0-or-later
// Context-register call profiles used by presentation preparation.
// The consumer ignores the results; register values retain each writer's original masks.
#pragma once

#include "command_reservation.h"

#include "present_preparation_packets.h"

namespace render {
inline void write_present_register_packet(PacketBuffer *commands, uintptr_t cursor,
                                          uint32_t index, uint32_t value)
{
    auto *packet = reinterpret_cast<PreparationWordPacket *>(cursor);
    packet->header.set(set_context_reg_one_value_header);
    packet->index.set(index);
    packet->value.set(value);
    // Output can overlap command storage. Always publish from the captured cursor,
    // rather than reloading a cursor word overwritten by those three ordered stores.
    set_packet_cursor(commands, cursor + sizeof(PreparationWordPacket));
}
}  // namespace render
