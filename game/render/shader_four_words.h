// SPDX-License-Identifier: GPL-2.0-or-later
// Shared four-word descriptor/sampler emission; only the NOP control differs.
#pragma once
#include "command_flags.h"
#include "shader_packet_layout.h"
namespace render {
inline void emit_shader_four_words(PacketBuffer *commands, uint32_t stage,
                                  uint32_t slot, const void *descriptor, uint32_t control)
{
    constexpr uint32_t words = 8;
    constexpr uint32_t set_sh_reg_4_values_header = 0xc0047600;
    constexpr uint32_t remaining_bytes = 24;
    uintptr_t cursor;
    if (!reserve_words(commands, words, cursor)) return;
    const uint32_t flags = packet_flags(commands);
    const uint32_t base = shader_stage_base(stage);
    shader_write_descriptor_header(commands, cursor, flags, base, slot, control, set_sh_reg_4_values_header);
    auto *out = reinterpret_cast<ShaderDescriptorPacket *>(cursor);
    const auto *source = static_cast<const ShaderDescriptorWords *>(descriptor);
    const uint64_t low = source->words[0].get();
    const uint64_t high = source->words[1].get();
    out->payload[1].set(high);
    out->payload[0].set(low);
    // Output may have overwritten the cursor: this is a live add, not captured cursor+size.
    set_packet_cursor(commands, packet_cursor(commands) + remaining_bytes);
}
}  // namespace render
