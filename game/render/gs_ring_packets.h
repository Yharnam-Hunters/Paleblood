// SPDX-License-Identifier: GPL-2.0-or-later
// GS ring context packets and their canonical final-packet observation.
#pragma once

#include "packet_observation.h"
#include "present_dirty_preparation_packets.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace render::gs_ring {
constexpr uint32_t gs_stream_count = 4;
constexpr uint32_t itemsize_mask = 0x7fff, max_vertices_mask = 0x7ff;
constexpr uint32_t vgt_gs_vert_itemsize_index = 0x2d7;
constexpr uint32_t vgt_gsvs_ring_offset_1_index = 0x298;
constexpr uint32_t vgt_gs_max_vert_out_index = 0x2ce;
constexpr uint32_t vgt_gsvs_ring_itemsize_index = 0x2ac;
constexpr uint32_t set_context_reg_four_values_header = 0xc0046900;
constexpr uint32_t set_context_reg_three_values_header = 0xc0036900;

template <uint32_t Count> struct ContextValuesPacket {
    render::Field<uint32_t> header;
    render::Field<uint32_t> register_index;
    render::Field<uint32_t> values[Count];
};
using ItemsizePacket = ContextValuesPacket<gs_stream_count>;
using OffsetPacket = ContextValuesPacket<gs_stream_count - 1>;
using ScalarPacket = render::PreparationCommandPacket;
static_assert(sizeof(ItemsizePacket) == 24);
static_assert(sizeof(OffsetPacket) == 20);
static_assert(sizeof(std::array<uint32_t, gs_stream_count>) == gs_stream_count * sizeof(uint32_t));
constexpr uint32_t reservation_words =
    (sizeof(ItemsizePacket) + sizeof(OffsetPacket) + 2 * sizeof(ScalarPacket)) / render::packet_word_bytes;
constexpr uintptr_t scalar_packets_bytes = 2 * sizeof(ScalarPacket);

template <typename Packet> inline bool gs_ring_packet_shape(const Packet *packet, uint32_t header, uint32_t index)
{
    return packet->header.get() == header && packet->register_index.get() == index;
}

inline void observe_gs_ring_sizes(const render::PacketObservation &call, const ItemsizePacket *items,
                        const OffsetPacket *offsets, const ScalarPacket *maximum, const ScalarPacket *total)
{
    if (!call.available) return;
    if (!gs_ring_packet_shape(items, set_context_reg_four_values_header, vgt_gs_vert_itemsize_index) ||
        !gs_ring_packet_shape(offsets, set_context_reg_three_values_header, vgt_gsvs_ring_offset_1_index) ||
        !gs_ring_packet_shape(maximum, render::preparation_packet::set_context_reg_one_value_header, vgt_gs_max_vert_out_index) ||
        !gs_ring_packet_shape(total, render::preparation_packet::set_context_reg_one_value_header, vgt_gsvs_ring_itemsize_index)) {
        std::fputs("GS-ring observer: final packet has an unsupported header or register index\n", stderr);
        std::abort();
    }
    // The original observer gives all nine events one call identity and interleaves
    // item sizes with offsets. Read the actual final payloads in that same order.
    for (uint32_t stream = 0; stream < gs_stream_count; ++stream) {
        rt_observe_register(call.buffer, call.offset,
                            render::context_register_base + vgt_gs_vert_itemsize_index + stream,
                            items->values[stream].get());
        if (stream + 1 < gs_stream_count)
            rt_observe_register(call.buffer, call.offset,
                                render::context_register_base + vgt_gsvs_ring_offset_1_index + stream,
                                offsets->values[stream].get());
    }
    rt_observe_register(call.buffer, call.offset,
                        render::context_register_base + vgt_gs_max_vert_out_index, maximum->payload.get());
    rt_observe_register(call.buffer, call.offset,
                        render::context_register_base + vgt_gsvs_ring_itemsize_index, total->payload.get());
}
}  // namespace render::gs_ring
