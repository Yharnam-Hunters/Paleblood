// SPDX-License-Identifier: GPL-2.0-or-later
// Report the final depth/stencil packets after cursor publication, without rereading descriptors.
#pragma once

#include <array>
#include <cstring>

#include "packet_observation.h"
#include "present_preparation_packets.h"

namespace render {
namespace depth_target_packet {
constexpr uint32_t block_header = 0xc0086900, block_index = 0x10, block_words = 8;
constexpr uint32_t depth_info_index = 0x0f, depth_view_index = 2;
constexpr uint32_t htile_data_base_index = 5, htile_surface_index = 0x2af;
constexpr uint32_t stencil_info_index = 0x11;
constexpr std::size_t single_packets = 4;
}

inline uint32_t depth_packet_offset(const PacketObservation &call, const void *packet)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(packet);
    if (address < call.buffer || address - call.buffer > UINT32_MAX) {
        std::fputs("depth-target observer: packet offset is outside the recording format\n", stderr);
        std::abort();
    }
    return static_cast<uint32_t>(address - call.buffer);
}

inline void require_depth_single_packet(const PreparationCommandPacket *packet, uint32_t index)
{
    if (packet->header.get() != preparation_packet::set_context_reg_one_value_header ||
            packet->register_index.get() != index) {
        std::fputs("depth-target observer: final singleton packet does not select its depth register\n", stderr);
        std::abort();
    }
}

inline void record_depth_target_block(const PacketObservation &call, const DepthTargetPacket *packet,
                                      const DepthTargetPacket *trailing)
{
    if (!call.available) return;
    using namespace depth_target_packet;
    if (packet->header.get() != block_header || packet->index.get() != block_index) {
        std::fputs("depth-target observer: final packet is not the supported depth register block\n", stderr);
        std::abort();
    }
    const std::array<const PreparationCommandPacket *, single_packets> singles{
        &trailing->depth_info, &trailing->depth_view, &trailing->htile_data_base, &trailing->htile_surface};
    constexpr std::array<uint32_t, single_packets> indices{
        depth_info_index, depth_view_index, htile_data_base_index, htile_surface_index};
    std::array<uint32_t, single_packets> offsets, values;
    const uint32_t first_offset = depth_packet_offset(call, packet);
    constexpr uint32_t block_bytes = offsetof(DepthTargetPacket, depth_info);
    uint64_t previous_end = uint64_t(first_offset) + block_bytes;
    // Validate the complete shape before recording any part of the target.
    for (std::size_t i = 0; i < singles.size(); ++i) {
        require_depth_single_packet(singles[i], indices[i]);
        offsets[i] = depth_packet_offset(call, singles[i]);
        if (offsets[i] < previous_end) {
            std::fputs("depth-target observer: final register packets overlap or run backwards\n", stderr);
            std::abort();
        }
        previous_end = uint64_t(offsets[i]) + sizeof(PreparationCommandPacket);
        values[i] = singles[i]->payload.get();
    }
    std::array<uint32_t, block_words> words;
    std::memcpy(words.data(), &packet->z_and_stencil_info, sizeof(words));
    for (std::size_t i = 0; i < words.size(); ++i)
        rt_observe_register(call.buffer, first_offset, RT_CONTEXT_REGISTERS_FIRST + block_index + i, words[i]);
    for (std::size_t i = 0; i < singles.size(); ++i)
        rt_observe_register(call.buffer, offsets[i], RT_CONTEXT_REGISTERS_FIRST + indices[i], values[i]);
}

inline void record_depth_target_clear(const PacketObservation &call, const ClearDepthTargetPacket *packet)
{
    if (!call.available) return;
    using namespace depth_target_packet;
    require_depth_single_packet(&packet->z_info, block_index);
    require_depth_single_packet(&packet->stencil_info, stencil_info_index);
    if (packet->z_info.payload.get() != 0 || packet->stencil_info.payload.get() != 0) {
        std::fputs("depth-target observer: final clear packet does not disable depth and stencil\n", stderr);
        std::abort();
    }
    const uint32_t z_offset = depth_packet_offset(call, &packet->z_info);
    const uint32_t stencil_offset = depth_packet_offset(call, &packet->stencil_info);
    rt_observe_register(call.buffer, z_offset, RT_CONTEXT_REGISTERS_FIRST + block_index, 0);
    rt_observe_register(call.buffer, stencil_offset, RT_CONTEXT_REGISTERS_FIRST + stencil_info_index, 0);
}
}  // namespace render
