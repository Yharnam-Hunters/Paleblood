// SPDX-License-Identifier: GPL-2.0-or-later
// Pending preparation events and optional range state consumed before shader bindings.
#pragma once
#include "runtime/original.h"

#include "shader_preparation_state.h"

namespace render {
struct PacketBuffer;
RT_ORIGINAL(0x02ab09e0, emit_shader_preparation_barrier,
            void(PacketBuffer *, uint32_t, uint32_t));
RT_ORIGINAL(0x01475e10, emit_shader_preparation_range,
            void(PacketBuffer *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x01477f40, emit_shader_preparation_event,
            void(PacketBuffer *, uint32_t, uint32_t, uint32_t));
}  // namespace render
