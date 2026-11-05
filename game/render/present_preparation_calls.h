// SPDX-License-Identifier: GPL-2.0-or-later
// The last-record dirty-state updater writes the depth and stencil clear registers
// through these original packet writers, then caches the values they leave behind.
#pragma once

#include "command_packets.h"
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x01473590, render_emit_present_preparation_scalar,
            uint64_t(PacketBuffer *, float));
RT_ORIGINAL(0x014735f0, render_emit_present_preparation_byte,
            uint64_t(PacketBuffer *, uint32_t));
}  // namespace render
