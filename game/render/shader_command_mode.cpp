// SPDX-License-Identifier: GPL-2.0-or-later
// Write VGT_SHADER_STAGES_EN for an admitted stage selector after capacity admission.
#include "shader_packet_layout.h"

namespace {
// Hardware stage combinations from VGT_SHADER_STAGES_EN. Bit9 is not described
// in the pinned gfx7 register table; preserve its admitted value without naming a role.
constexpr uint32_t local_hull_domain_stages = 0x45;
constexpr uint32_t local_hull_domain_export_geometry_copy_stages = 0xad;
constexpr uint32_t export_geometry_copy_stages = 0xb0;
constexpr uint32_t unidentified_stage_bit9 = 0x200;
}

extern "C" void bb_render_select_shader_command_mode(render::PacketBuffer *commands,
                                                      uint32_t mode)
{
    using namespace render;
    constexpr uint32_t words = 3;
    constexpr uint32_t set_context_reg_one_value_header = 0xc0016900;
    constexpr uint32_t shader_stages_enable_index = 0x2d5;
    uintptr_t cursor;
    if (!reserve_words(commands, words, cursor)) return;
    switch (mode) {
    case 0: case local_hull_domain_stages:
    case local_hull_domain_export_geometry_copy_stages:
    case export_geometry_copy_stages: case unidentified_stage_bit9: break;
    default: return;
    }
    auto *out = reinterpret_cast<ShaderPacketWords *>(cursor);
    out->header.set(set_context_reg_one_value_header);
    out->register_index.set(shader_stages_enable_index);
    out->value_low.set(mode);
    set_packet_cursor(commands, cursor + words * shader_packet_word_bytes);
}
