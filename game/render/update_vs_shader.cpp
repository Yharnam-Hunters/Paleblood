// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve a vertex-shader update and forward its opaque registers and modifier to GNM.
// A successful call advances the live cursor, retaining any library writes to it.
#include "command_reservation.h"
#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x02fc04e8, gnm_update_vs_shader,
            int32_t(void *commands, uint32_t words, const void *registers,
                    uint32_t shader_modifier));
constexpr uint32_t vs_shader_words = 29;
}  // namespace

extern "C" void bb_render_update_vs_shader(render::PacketBuffer *commands,
                                             const void *registers, uint32_t shader_modifier)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, vs_shader_words, cursor)) return;

    const int32_t result = gnm_update_vs_shader(reinterpret_cast<void *>(cursor),
                                               vs_shader_words, registers, shader_modifier);
    if (result == 0)
        render::set_packet_cursor(commands, render::packet_cursor(commands) +
                                               vs_shader_words * render::packet_word_bytes);
}
