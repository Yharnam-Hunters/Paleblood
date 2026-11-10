// SPDX-License-Identifier: GPL-2.0-or-later
// Shader cleanup and binding select the command mode through this original helper.
#pragma once
#include "command_packets.h"
#include "runtime/original.h"
namespace render {
RT_ORIGINAL(0x01476030, select_shader_command_mode, void(PacketBuffer *, uint32_t));
}  // namespace render
