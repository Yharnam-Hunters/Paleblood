// SPDX-License-Identifier: GPL-2.0-or-later
// Set geometry-shader mode and reset streamout configuration for raw mode zero.
// Both observations belong to the admitted call position, after the final publication.
#include "packet_observation.h"
#include "present_register_packets.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t vgt_gs_mode_index = 0x290;
constexpr uint32_t vgt_strmout_config_index = 0x2e5;
constexpr uint32_t gs_cut_mode_mask = 0x30;
constexpr unsigned gs_cut_mode_shift = 4;
constexpr uint32_t register_packet_words =
    sizeof(render::PreparationCommandPacket) / render::packet_word_bytes;

void observe_gs_mode(const render::PacketObservation &call, uintptr_t cursor, bool reset_streamout)
{
    if (!call.available) return;
    const auto *mode = reinterpret_cast<const render::PreparationCommandPacket *>(cursor);
    const auto *streamout = mode + 1;
    const auto header = render::preparation_packet::set_context_reg_one_value_header;
    if (mode->header.get() != header || mode->register_index.get() != vgt_gs_mode_index ||
        (reset_streamout && (streamout->header.get() != header ||
                            streamout->register_index.get() != vgt_strmout_config_index))) {
        std::fputs("GS-mode observer: final packet has an unsupported header or register index\n", stderr);
        std::abort();
    }
    rt_observe_register(call.buffer, call.offset, render::context_register_base + mode->register_index.get(),
                        mode->payload.get());
    if (reset_streamout)
        rt_observe_register(call.buffer, call.offset, render::context_register_base + streamout->register_index.get(),
                            streamout->payload.get());
}
}  // namespace

extern "C" void bb_render_set_gs_mode(render::PacketBuffer *commands, uint32_t mode, uint32_t cut_mode)
{
    const bool reset_streamout = mode == 0;
    const uint32_t requested = reset_streamout ? 2 * register_packet_words : register_packet_words;
    uintptr_t cursor;
    if (!render::reserve_words(commands, requested, cursor)) return;

    const auto call = render::observe_packet(commands, cursor);
    const uint32_t value = (mode & ~gs_cut_mode_mask) | ((cut_mode << gs_cut_mode_shift) & gs_cut_mode_mask);
    render::write_present_register_packet(commands, cursor, vgt_gs_mode_index, value);
    if (reset_streamout)
        render::write_present_register_packet(commands, cursor + sizeof(render::PreparationCommandPacket),
                                              vgt_strmout_config_index, 0);

    // Cursor publication can overlap either payload; observe their final words.
    observe_gs_mode(call, cursor, reset_streamout);
}
