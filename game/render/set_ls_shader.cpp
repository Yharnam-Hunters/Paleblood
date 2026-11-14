// SPDX-License-Identifier: GPL-2.0-or-later
// Set a local shader through GNM after reserving packet space.
// A successful call advances the live cursor, retaining any library writes to it.
#include "command_reservation.h"
#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x02fc0528, gnm_set_ls_shader,
            int32_t(void *commands, uint32_t words, const void *registers,
                    uint32_t shader_modifier));
constexpr uint32_t ls_shader_words = 23;
}  // namespace

extern "C" void bb_render_set_ls_shader(render::PacketBuffer *commands,
                                       const void *registers, uint32_t shader_modifier)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, ls_shader_words, cursor)) return;

    const int32_t result = gnm_set_ls_shader(reinterpret_cast<void *>(cursor),
                                           ls_shader_words, registers, shader_modifier);
    if (result == 0)
        render::set_packet_cursor(commands, render::packet_cursor(commands) +
                                               ls_shader_words * render::packet_word_bytes);
}
