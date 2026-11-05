// SPDX-License-Identifier: GPL-2.0-or-later
// Write 8 consecutive shader-register values at the original stage/slot-derived register index.
#include "command_flags.h"
#include "shader_packet_layout.h"

namespace {
constexpr uint32_t last_descriptor_word = 3;
constexpr uint32_t upper_pair_begin = 2;
}

extern "C" void bb_render_emit_shader_descriptor32(render::PacketBuffer *commands,
                                                uint32_t stage, uint32_t slot, const void *descriptor)
{
    using namespace render;
    constexpr uint32_t words = 12;
    constexpr uint32_t control = 0x68750005;
    constexpr uint32_t set_sh_reg_8_values_header = 0xc0087600;
    constexpr uint32_t remaining_bytes = 40;
    uintptr_t cursor;
    if (!reserve_words(commands, words, cursor)) return;
    const uint32_t flags = packet_flags(commands);
    const uint32_t base = shader_stage_base(stage);
    shader_write_descriptor_header(commands, cursor, flags, base, slot, control, set_sh_reg_8_values_header);
    auto *out = reinterpret_cast<ShaderDescriptorPacket *>(cursor);
    const auto *source = static_cast<const ShaderDescriptorWords *>(descriptor);
    // The upper pair is copied separately, before either lower word is loaded.
    out->payload[last_descriptor_word].set(source->words[last_descriptor_word].get());
    out->payload[upper_pair_begin].set(source->words[upper_pair_begin].get());
    const uint64_t low = source->words[0].get();
    const uint64_t high = source->words[1].get();
    out->payload[1].set(high);
    out->payload[0].set(low);
    // Output may have overwritten the cursor: this is a live add, not captured cursor+size.
    set_packet_cursor(commands, packet_cursor(commands) + remaining_bytes);
}
