// SPDX-License-Identifier: GPL-2.0-or-later
// Emit four SET_SH_REG values with the descriptor's ordered copy semantics.
#include "shader_four_words.h"
extern "C" void bb_render_emit_shader_descriptor16(render::PacketBuffer *commands,
    uint32_t stage, uint32_t slot, const void *descriptor)
{
    constexpr uint32_t descriptor_control = 0x68750004;
    render::emit_shader_four_words(commands, stage, slot, descriptor, descriptor_control);
}
