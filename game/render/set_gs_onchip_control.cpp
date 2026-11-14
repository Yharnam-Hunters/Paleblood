// SPDX-License-Identifier: GPL-2.0-or-later
// Set geometry-shader subgroup sizes in VGT_GS_ONCHIP_CNTL.
// Each input contributes its low eleven bits; higher bits are discarded.
#include "present_register_packets.h"

namespace {
constexpr uint32_t vgt_gs_onchip_cntl_index = 0x291;
constexpr uint32_t es_verts_per_subgrp_mask = 0x7ff;
constexpr uint32_t gs_prims_per_subgrp_mask = 0x3ff800;
constexpr unsigned gs_prims_per_subgrp_shift = 11;
constexpr uint32_t register_packet_words =
    sizeof(render::PreparationCommandPacket) / render::packet_word_bytes;
}  // namespace

extern "C" void bb_render_set_gs_onchip_control(render::PacketBuffer *commands,
                                               uint32_t es_verts_per_subgrp,
                                               uint32_t gs_prims_per_subgrp)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, register_packet_words, cursor)) return;

    const uint32_t value = (es_verts_per_subgrp & es_verts_per_subgrp_mask) |
                           ((gs_prims_per_subgrp << gs_prims_per_subgrp_shift) & gs_prims_per_subgrp_mask);
    render::write_present_register_packet(commands, cursor, vgt_gs_onchip_cntl_index, value);
}
