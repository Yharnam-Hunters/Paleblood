// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the packet writers that set or update a shader in a command buffer
// (command_packets.h). Each reserves the words its packets take (command_reservation.h: the
// space callback is asked when too few are left; a refusal writes nothing, a grant is trusted
// without checking the limit again), then lets the graphics library write them at the cursor
// and moves the cursor past them only when the library reports success:
// - render_emit_set_ps_shader_014753c0 (0x014753c0): sceGnmSetPsShader, 40 words;
// - render_emit_update_ps_shader_01475420 (0x01475420): sceGnmUpdatePsShader, 40 words;
// - render_emit_set_vs_shader_01475480 (0x01475480): sceGnmSetVsShader with a shader modifier,
//   29 words.
#include <cstdint>

#include "command_packets.h"
#include "command_reservation.h"
#include "runtime/original.h"

namespace {

using render::PacketBuffer;

RT_ORIGINAL(0x02fc04b8, gnm_set_ps_shader, int32_t(void *commands, uint32_t words, const void *registers));
RT_ORIGINAL(0x02fc04c8, gnm_update_ps_shader, int32_t(void *commands, uint32_t words, const void *registers));
RT_ORIGINAL(0x02fc04d8, gnm_set_vs_shader,
            int32_t(void *commands, uint32_t words, const void *registers, uint32_t shader_modifier));

constexpr uint32_t ps_shader_words = 40;
constexpr uint32_t vs_shader_words = 29;

void *packet_at(uintptr_t cursor) { return reinterpret_cast<void *>(cursor); }

void advance(PacketBuffer *buffer, uint32_t words)
{
    render::set_packet_cursor(buffer, render::packet_cursor(buffer) + words * render::packet_word_bytes);
}

}  // namespace

extern "C" void bb_render_emit_set_ps_shader_014753c0(PacketBuffer *buffer, const void *registers)
{
    uintptr_t cursor;
    if (!render::reserve_words(buffer, ps_shader_words, cursor))
        return;
    if (gnm_set_ps_shader(packet_at(cursor), ps_shader_words, registers) == 0)
        advance(buffer, ps_shader_words);
}

extern "C" void bb_render_emit_update_ps_shader_01475420(PacketBuffer *buffer, const void *registers)
{
    uintptr_t cursor;
    if (!render::reserve_words(buffer, ps_shader_words, cursor))
        return;
    if (gnm_update_ps_shader(packet_at(cursor), ps_shader_words, registers) == 0)
        advance(buffer, ps_shader_words);
}

extern "C" void bb_render_emit_set_vs_shader_01475480(PacketBuffer *buffer, const void *registers,
                                                      uint32_t shader_modifier)
{
    uintptr_t cursor;
    if (!render::reserve_words(buffer, vs_shader_words, cursor))
        return;
    if (gnm_set_vs_shader(packet_at(cursor), vs_shader_words, registers, shader_modifier) == 0)
        advance(buffer, vs_shader_words);
}
