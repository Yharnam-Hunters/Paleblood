// SPDX-License-Identifier: GPL-2.0-or-later
// Packet writers share the command-buffer prefix from command_packets.h. Byte
// copies also support the unaligned and overlapping storage used by some callers.
#pragma once

#include <cstring>

#include "command_packets.h"

namespace render {

template <typename T> struct Field {
    uint8_t bytes[sizeof(T)];

    T get() const
    {
        T value;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    void set(T value) { std::memcpy(bytes, &value, sizeof(value)); }
};

inline uintptr_t packet_begin(const PacketBuffer *commands)
{
    uintptr_t value;
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    std::memcpy(&value, bytes + offsetof(PacketBuffer, begin), sizeof(value));
    return value;
}

inline uintptr_t packet_limit(const PacketBuffer *commands)
{
    uintptr_t value;
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    std::memcpy(&value, bytes + offsetof(PacketBuffer, limit), sizeof(value));
    return value;
}

inline uintptr_t packet_cursor(const PacketBuffer *commands)
{
    uintptr_t value;
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    std::memcpy(&value, bytes + offsetof(PacketBuffer, cursor), sizeof(value));
    return value;
}

inline PacketSpaceCallback packet_space_callback(const PacketBuffer *commands)
{
    PacketSpaceCallback value;
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    std::memcpy(&value, bytes + offsetof(PacketBuffer, space_callback), sizeof(value));
    return value;
}

inline uintptr_t packet_callback_argument(const PacketBuffer *commands)
{
    uintptr_t value;
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    std::memcpy(&value, bytes + offsetof(PacketBuffer, callback_argument), sizeof(value));
    return value;
}

inline void set_packet_begin(PacketBuffer *commands, uintptr_t value)
{
    auto *bytes = reinterpret_cast<uint8_t *>(commands);
    std::memcpy(bytes + offsetof(PacketBuffer, begin), &value, sizeof(value));
}

inline void set_packet_limit(PacketBuffer *commands, uintptr_t value)
{
    auto *bytes = reinterpret_cast<uint8_t *>(commands);
    std::memcpy(bytes + offsetof(PacketBuffer, limit), &value, sizeof(value));
}

inline void set_packet_cursor(PacketBuffer *commands, uintptr_t value)
{
    auto *bytes = reinterpret_cast<uint8_t *>(commands);
    std::memcpy(bytes + offsetof(PacketBuffer, cursor), &value, sizeof(value));
}

inline bool reserve_words(PacketBuffer *commands, uint32_t requested, uintptr_t &cursor)
{
    const uintptr_t limit = packet_limit(commands);
    cursor = packet_cursor(commands);
    const uint32_t available = static_cast<uint32_t>((limit - cursor) / packet_word_bytes);
    if (available >= requested) return true;

    // Capture the argument before the callback target. A successful callback may
    // replace the storage; use its cursor without checking the capacity again.
    const uintptr_t argument = packet_callback_argument(commands);
    const auto callback = packet_space_callback(commands);
    if (callback(commands, requested, argument) == 0) return false;
    cursor = packet_cursor(commands);
    return true;
}

}  // namespace render
