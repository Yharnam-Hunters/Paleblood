// SPDX-License-Identifier: GPL-2.0-or-later
// The command-buffer prefix used by packet writers. Cursor and limit are integer addresses
// because the original subtracts them with wrapping address arithmetic before checking space.
#pragma once

#include <cstddef>
#include <cstdint>

namespace render {

struct PacketBuffer;
// Packet writers consume only the low result byte. Zero declines the request; a nonzero
// result lets the writer reload the cursor and proceed without checking the limit again.
using PacketSpaceCallback = uint8_t (*)(PacketBuffer *, uint32_t requested_words, uintptr_t argument);

struct PacketBuffer {
    uintptr_t begin;
    uintptr_t limit;
    uintptr_t cursor;
    PacketSpaceCallback space_callback;
    uintptr_t callback_argument;
};
static_assert(sizeof(uintptr_t) == 8);
static_assert(offsetof(PacketBuffer, begin) == 0x0);
static_assert(offsetof(PacketBuffer, limit) == 0x8);
static_assert(offsetof(PacketBuffer, cursor) == 0x10);
static_assert(offsetof(PacketBuffer, space_callback) == 0x18);
static_assert(offsetof(PacketBuffer, callback_argument) == 0x20);
static_assert(sizeof(PacketBuffer) == 0x28);

constexpr uint32_t packet_word_bytes = 4;

inline uint32_t packet_available_words(const PacketBuffer *buffer)
{
    const uintptr_t bytes = buffer->limit - buffer->cursor;
    return static_cast<uint32_t>(bytes / packet_word_bytes);
}

}  // namespace render
