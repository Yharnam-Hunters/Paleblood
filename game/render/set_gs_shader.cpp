// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve a geometry-shader packet and forward its opaque register block to GNM.
// A successful call advances the live cursor, retaining any library writes to it.
#include "command_reservation.h"
#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x02fc0548, gnm_set_gs_shader,
            int32_t(void *commands, uint32_t words, const void *registers));
constexpr uint32_t gs_shader_words = 29;
}  // namespace

extern "C" void bb_render_set_gs_shader(render::PacketBuffer *commands,
                                      const void *registers)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, gs_shader_words, cursor)) return;

    const int32_t result = gnm_set_gs_shader(reinterpret_cast<void *>(cursor),
                                            gs_shader_words, registers);
    if (result == 0)
        render::set_packet_cursor(commands, render::packet_cursor(commands) +
                                               gs_shader_words * render::packet_word_bytes);
}
