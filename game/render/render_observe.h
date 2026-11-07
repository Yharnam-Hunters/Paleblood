// SPDX-License-Identifier: GPL-2.0-or-later
// Where a GPU library replacement's observer call says it was made (runtime/render_observer.h): the
// command buffer's start and the bytes it held, from the packet buffer and the cursor the
// replacement reserved its packet at. Register calls name AMD's dword register addresses: the
// context registers from 0xa000 and the user config registers from 0xc000 (gfx_7_2_d.h), the
// packets' register indexes counting from those bases.
#pragma once

#include "command_packets.h"
#include "runtime/render_observer.h"

#include <cstdint>

namespace render {

inline uint64_t observed_buffer(const PacketBuffer *commands) { return commands->begin; }

inline uint32_t observed_offset(const PacketBuffer *commands, uintptr_t cursor)
{
    return static_cast<uint32_t>(cursor - commands->begin);
}

constexpr uint32_t context_register_base = 0xa000;
constexpr uint32_t uconfig_register_base = 0xc000;

inline void observe_context_register(const PacketBuffer *commands, uintptr_t cursor, uint32_t index, uint32_t value)
{
    rt_observe_register(observed_buffer(commands), observed_offset(commands, cursor), context_register_base + index, value);
}

}  // namespace render
