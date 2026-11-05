// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve PS/VS command space and invoke the GNM shader packet writers.
#pragma once
#include "command_reservation.h"
#include "shader_packet_layout.h"
#include "runtime/original.h"
namespace render {
// GNM writers return a 32-bit status; only zero publishes the reserved space.
RT_ORIGINAL(0x02fc04d8, sceGnmSetVsShader,
            uint32_t(void *, uint32_t, const void *, uint32_t));
RT_ORIGINAL(0x02fc04b8, sceGnmSetPsShader,
            uint32_t(void *, uint32_t, const void *));
}  // namespace render
