// SPDX-License-Identifier: GPL-2.0-or-later
// Command-buffer prefix plus the auxiliary words copied by submission paths.
#pragma once

#include "command_reservation.h"

namespace render {
struct CommandBufferSnapshot {
    PacketBuffer commands;
    uint64_t unknown_0x28;
    uint64_t unknown_0x30;
    uint32_t capacity_words;
    uint32_t unknown_0x3c;
};
static_assert(offsetof(CommandBufferSnapshot, commands) == 0x0);
static_assert(offsetof(CommandBufferSnapshot, unknown_0x28) == 0x28);
static_assert(offsetof(CommandBufferSnapshot, capacity_words) == 0x38);
static_assert(offsetof(CommandBufferSnapshot, unknown_0x3c) == 0x3c);
static_assert(sizeof(CommandBufferSnapshot) == 0x40);

}  // namespace render
