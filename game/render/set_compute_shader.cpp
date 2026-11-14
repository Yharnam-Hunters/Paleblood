// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve a compute-shader packet and forward its opaque register block to GNM.
// A successful library call advances the live cursor, including any library writes.
#include "command_reservation.h"
#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x02fc0538, gnm_set_cs_shader,
            int32_t(void *commands, uint32_t words, const void *registers));
constexpr uint32_t cs_shader_words = 25;
}  // namespace

extern "C" void bb_render_set_compute_shader(render::PacketBuffer *commands,
                                               const void *registers)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, cs_shader_words, cursor)) return;

    const int32_t result = gnm_set_cs_shader(reinterpret_cast<void *>(cursor),
                                            cs_shader_words, registers);
    if (result == 0)
        render::set_packet_cursor(commands, render::packet_cursor(commands) +
                                               cs_shader_words * render::packet_word_bytes);
}
