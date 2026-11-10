// SPDX-License-Identifier: GPL-2.0-or-later
// The color-target replacement reports final packets through the canonical runtime observer.
#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "render_observe.h"
#include "present_preparation_packets.h"

namespace render {
namespace color_target_packet {
constexpr uint32_t register_stride = 15;
constexpr uint32_t base_index = 0x318;
constexpr uint32_t info_index = 0x31c;
constexpr uint32_t block_header = 0xc00b6900;
}

struct ColorTargetObservation {
    uint64_t buffer{};
    uint32_t offset{};
    bool available{};
};

inline ColorTargetObservation observe_color_target_packet(const PacketBuffer *commands, uintptr_t cursor)
{
    // The verifier and hosts without the renderer record must not gain a begin-field read.
    if (!rt_render_observer_available()) return {};
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    const uintptr_t begin = Field<uintptr_t>::read(bytes + offsetof(PacketBuffer, begin));
    if (cursor < begin || cursor - begin > UINT32_MAX) {
        std::fputs("color-target observer: command offset is outside the recording format\n", stderr);
        std::abort();
    }
    return {begin, static_cast<uint32_t>(cursor - begin), true};
}

inline unsigned observed_color_target_slot(uint32_t index, uint32_t first)
{
    const uint32_t delta = index - first;
    if (index < first || delta % color_target_packet::register_stride != 0 ||
        delta / color_target_packet::register_stride >= RT_COLOR_TARGETS) {
        std::fputs("color-target observer: final register does not select a supported color-target slot\n", stderr);
        std::abort();
    }
    return delta / color_target_packet::register_stride;
}

inline void record_color_target_block(const ColorTargetObservation &observation, const ColorTargetPacket *packet)
{
    if (!observation.available) return;
    if (packet->header.get() != color_target_packet::block_header) {
        std::fputs("color-target observer: final packet is not a supported color-target register block\n", stderr);
        std::abort();
    }
    const unsigned slot = observed_color_target_slot(packet->index.get(), color_target_packet::base_index);
    std::array<uint32_t, RT_COLOR_TARGET_WORDS> words;
    std::memcpy(words.data(), &packet->base_and_pitch, words.size() * sizeof(uint32_t));
    rt_observe_color_target(observation.buffer, observation.offset, slot, words.data());
}

inline void record_color_target_clear(const ColorTargetObservation &observation, const PreparationCommandPacket *packet)
{
    if (!observation.available) return;
    if (packet->header.get() != preparation_packet::set_context_reg_one_value_header || packet->payload.get() != 0) {
        std::fputs("color-target observer: final clear packet cannot be represented as a disabled target\n", stderr);
        std::abort();
    }
    const unsigned slot = observed_color_target_slot(packet->register_index.get(), color_target_packet::info_index);
    rt_observe_color_target(observation.buffer, observation.offset, slot, nullptr);
}
}  // namespace render
