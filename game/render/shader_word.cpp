// SPDX-License-Identifier: GPL-2.0-or-later
// Write 1 consecutive shader-register values at the original stage/slot-derived register index.
#include "command_flags.h"
#include "shader_packet_layout.h"

extern "C" void bb_render_emit_shader_word(render::PacketBuffer *commands,
                                            uint32_t stage, uint32_t slot, uint32_t value)
{
    using namespace render;
    constexpr uint32_t words = 3;
    constexpr uint32_t set_sh_reg_1_values_header = 0xc0017600;
    uintptr_t cursor;
    if (!reserve_words(commands, words, cursor)) return;
    const uint32_t flags = packet_flags(commands);
    const uint32_t base = shader_stage_base(stage);
    auto *out = reinterpret_cast<ShaderPacketWords *>(cursor);
    out->header.set(set_sh_reg_1_values_header | (flags & shader_packet_flag_mask));
    out->register_index.set(base + slot - shader_register_origin);
    out->value_low.set(value);
    set_packet_cursor(commands, cursor + words * shader_packet_word_bytes);
}
