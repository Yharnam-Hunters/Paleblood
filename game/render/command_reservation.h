// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve packet space, retaining the callback's cursor and admission rules.
#pragma once

#include "byte_field.h"
#include "command_packets.h"

namespace render {
inline uintptr_t packet_limit(const PacketBuffer *commands)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    return Field<uintptr_t>::read(bytes + offsetof(PacketBuffer, limit));
}

inline uintptr_t packet_cursor(const PacketBuffer *commands)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    return Field<uintptr_t>::read(bytes + offsetof(PacketBuffer, cursor));
}

template <typename Callback> inline Callback packet_callback(const PacketBuffer *commands)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    return Field<Callback>::read(bytes + offsetof(PacketBuffer, space_callback));
}

inline uintptr_t packet_callback_argument(const PacketBuffer *commands)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    return Field<uintptr_t>::read(bytes + offsetof(PacketBuffer, callback_argument));
}

inline void set_packet_cursor(PacketBuffer *commands, uintptr_t value)
{
    auto *bytes = reinterpret_cast<uint8_t *>(commands);
    Field<uintptr_t>::write(bytes + offsetof(PacketBuffer, cursor), value);
}

template <typename Callback>
inline bool reserve_packet_words(PacketBuffer *commands, uint32_t requested,
                                 uintptr_t &cursor, uint64_t &callback_result)
{
    // Read through byte views before using the shared capacity calculation: the
    // caller's prefix may be unaligned or overlap the packet being written.
    PacketBuffer capacity{};
    capacity.limit = packet_limit(commands);
    capacity.cursor = packet_cursor(commands);
    cursor = capacity.cursor;
    if (packet_available_words(&capacity) >= requested) return true;

    // Capture the argument before the callback target. A successful callback may
    // replace storage; use its cursor without checking capacity again.
    const uintptr_t argument = packet_callback_argument(commands);
    const auto callback = packet_callback<Callback>(commands);
    callback_result = callback(commands, requested, argument);
    if (static_cast<uint8_t>(callback_result) == 0) return false;
    cursor = packet_cursor(commands);
    return true;
}

inline bool reserve_words(PacketBuffer *commands, uint32_t requested, uintptr_t &cursor)
{
    uint64_t callback_result = 0;
    return reserve_packet_words<PacketSpaceCallback>(commands, requested, cursor, callback_result);
}
}  // namespace render
