// SPDX-License-Identifier: GPL-2.0-or-later
// Four-word presentation slot packet emitted through the shared command buffer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "command_packets.h"

namespace render {

struct PresentSlotPacket {
    uint32_t header;
    uint32_t register_index;
    uint64_t value;
};
static_assert(offsetof(PresentSlotPacket, header) == 0x0);
static_assert(offsetof(PresentSlotPacket, register_index) == 0x4);
static_assert(offsetof(PresentSlotPacket, value) == 0x8);
static_assert(sizeof(PresentSlotPacket) == 0x10);
}  // namespace render
