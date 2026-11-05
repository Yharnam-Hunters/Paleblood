// SPDX-License-Identifier: GPL-2.0-or-later
// Reserve 29 dwords, call sceGnmSetVsShader and advance the live cursor only on status zero.
#include "shader_gnm_calls.h"

extern "C" void bb_render_set_vs_shader(render::PacketBuffer *commands,
                                                         const void *descriptor,
                                                         uint32_t modifier)
{
    using namespace render;
    constexpr uint32_t words = 29;
    uintptr_t cursor;
    if (!reserve_words(commands, words, cursor)) return;
    if (sceGnmSetVsShader(reinterpret_cast<void *>(cursor), words,
                                    descriptor, modifier) != 0) return;
    // The original adds to the live field, including changes made by the native writer.
    auto *packet = commands;
    set_packet_cursor(packet, packet_cursor(packet) + uint64_t(words) * shader_packet_word_bytes);
}
