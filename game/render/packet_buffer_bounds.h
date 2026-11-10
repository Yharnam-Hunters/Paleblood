// SPDX-License-Identifier: GPL-2.0-or-later
// Finish and pool operations copy or replace a packet buffer's storage bounds.
#pragma once

#include "byte_field.h"
#include "command_packets.h"

namespace render {
inline uintptr_t packet_begin(const PacketBuffer *commands)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    return Field<uintptr_t>::read(bytes + offsetof(PacketBuffer, begin));
}

inline void set_packet_begin(PacketBuffer *commands, uintptr_t value)
{
    auto *bytes = reinterpret_cast<uint8_t *>(commands);
    Field<uintptr_t>::write(bytes + offsetof(PacketBuffer, begin), value);
}

inline void set_packet_limit(PacketBuffer *commands, uintptr_t value)
{
    auto *bytes = reinterpret_cast<uint8_t *>(commands);
    Field<uintptr_t>::write(bytes + offsetof(PacketBuffer, limit), value);
}
}  // namespace render
