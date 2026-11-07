// SPDX-License-Identifier: GPL-2.0-or-later
// Packet writers that inspect draw/shader flags read one DWORD after PacketBuffer.
#pragma once
#include "command_reservation.h"
namespace render {
struct CommandPacketFlags {
    uint8_t commands[sizeof(PacketBuffer)];
    Field<uint32_t> flags;
};
static_assert(offsetof(CommandPacketFlags, flags) == 0x28);
static_assert(sizeof(CommandPacketFlags) == 0x2c);
inline uint32_t packet_flags(const PacketBuffer *commands)
{
    return reinterpret_cast<const CommandPacketFlags *>(commands)->flags.get();
}
}  // namespace render
