// SPDX-License-Identifier: GPL-2.0-or-later
// Set GS stream item sizes, ring offsets and totals with the original live reads.
#include "command_reservation.h"
#include "gs_ring_packets.h"

#include <array>
#include <cstring>

namespace ring = render::gs_ring;
namespace {
uint32_t read_gs_ring_itemsize(const uint32_t *sizes, uint32_t stream)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(sizes);
    return render::Field<uint32_t>::read(bytes + stream * sizeof(uint32_t));
}

}  // namespace

extern "C" void bb_render_set_gs_ring_sizes(render::PacketBuffer *commands, const uint32_t *sizes,
                                           uint32_t max_vertices)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, ring::reservation_words, cursor)) return;
    const auto call = render::observe_packet(commands, cursor);

    std::array<uint32_t, ring::gs_stream_count> snapshot;
    std::memcpy(snapshot.data(), sizes, sizeof(snapshot));
    std::array<uint32_t, ring::gs_stream_count - 1> products;
    uint32_t prefix = 0;
    for (uint32_t stream = 0; stream + 1 < ring::gs_stream_count; ++stream) {
        prefix += snapshot[stream];
        products[stream] = (prefix * max_vertices) & ring::itemsize_mask;
    }
    auto *items = reinterpret_cast<ring::ItemsizePacket *>(cursor);
    items->header.set(ring::set_context_reg_four_values_header);
    items->register_index.set(ring::vgt_gs_vert_itemsize_index);
    for (uint32_t stream = 0; stream < ring::gs_stream_count; ++stream)
        items->values[stream].set(snapshot[stream] & ring::itemsize_mask);

    // These cursor reads follow packet stores that can overlap the prefix.
    const uintptr_t offset_cursor = render::packet_cursor(commands) + sizeof(ring::ItemsizePacket);
    render::set_packet_cursor(commands, offset_cursor);
    auto *offsets = reinterpret_cast<ring::OffsetPacket *>(offset_cursor);
    offsets->header.set(ring::set_context_reg_three_values_header);
    offsets->register_index.set(ring::vgt_gsvs_ring_offset_1_index);
    for (uint32_t stream = 0; stream + 1 < ring::gs_stream_count; ++stream)
        offsets->values[stream].set(products[stream]);

    const uintptr_t scalar_cursor = render::packet_cursor(commands) + sizeof(ring::OffsetPacket);
    auto *maximum = reinterpret_cast<ring::ScalarPacket *>(scalar_cursor);
    maximum->header.set(render::preparation_packet::set_context_reg_one_value_header);
    maximum->register_index.set(ring::vgt_gs_max_vert_out_index);
    maximum->payload.set(max_vertices & ring::max_vertices_mask);

    // The total re-reads the descriptor after MAX stores, in the observed order.
    uint32_t total_size = read_gs_ring_itemsize(sizes, 1);
    const uint32_t third_stream_itemsize = read_gs_ring_itemsize(sizes, 2);
    total_size += read_gs_ring_itemsize(sizes, 0);
    total_size += third_stream_itemsize;
    total_size += read_gs_ring_itemsize(sizes, ring::gs_stream_count - 1);
    auto *total = reinterpret_cast<ring::ScalarPacket *>(scalar_cursor + sizeof(ring::ScalarPacket));
    total->header.set(render::preparation_packet::set_context_reg_one_value_header);
    total->register_index.set(ring::vgt_gsvs_ring_itemsize_index);
    total->payload.set((total_size * max_vertices) & ring::itemsize_mask);
    render::set_packet_cursor(commands, scalar_cursor + ring::scalar_packets_bytes);
    ring::observe_gs_ring_sizes(call, items, offsets, maximum, total);
}
