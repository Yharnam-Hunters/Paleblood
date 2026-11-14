// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve a hull-shader packet and forward its opaque register block to GNM.
// Read the configuration word after admission: the callback can change it.
#include "command_reservation.h"
#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x02fc0508, gnm_set_hs_shader,
            int32_t(void *commands, uint32_t words, const void *registers,
                    uint32_t configuration));
constexpr uint32_t hs_shader_words = 30;
}  // namespace

extern "C" void bb_render_set_hs_shader(render::PacketBuffer *commands,
                                      const void *registers, const uint32_t *configuration)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, hs_shader_words, cursor)) return;

    const uint32_t configuration_value = render::Field<uint32_t>::read(configuration);
    const int32_t result = gnm_set_hs_shader(reinterpret_cast<void *>(cursor),
                                            hs_shader_words, registers, configuration_value);
    if (result == 0)
        render::set_packet_cursor(commands, render::packet_cursor(commands) +
                                               hs_shader_words * render::packet_word_bytes);
}
