// SPDX-License-Identifier: GPL-2.0-or-later
// Save an admitted packet's identity before stores can overwrite the command prefix.
#pragma once

#include <cstdio>
#include <cstdlib>

#include "render_observe.h"
#include "byte_field.h"

namespace render {
struct PacketObservation {
    uint64_t buffer{};
    uint32_t offset{};
    bool available{};
};

inline PacketObservation observe_packet(const PacketBuffer *commands, uintptr_t cursor)
{
    // Hosts without the canonical record must not gain a command-buffer begin read.
    if (!rt_render_observer_available()) return {};
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    const uintptr_t begin = Field<uintptr_t>::read(bytes + offsetof(PacketBuffer, begin));
    if (cursor < begin || cursor - begin > UINT32_MAX) {
        std::fputs("packet observer: command offset is outside the recording format\n", stderr);
        std::abort();
    }
    return {begin, static_cast<uint32_t>(cursor - begin), true};
}
}  // namespace render
