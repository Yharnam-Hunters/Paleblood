// SPDX-License-Identifier: GPL-2.0-or-later
// Some packet writers return the callback's entire 64-bit result when it declines
// a reservation. Keep that result while testing only its low byte for admission.
#pragma once

#include "command_reservation.h"

namespace render {
using PacketCapacityResultCallback = uint64_t (*)(PacketBuffer *, uint32_t requested_words,
                                                 uintptr_t argument);
static_assert(sizeof(PacketCapacityResultCallback) == sizeof(PacketSpaceCallback));

inline PacketCapacityResultCallback packet_capacity_result_callback(const PacketBuffer *commands)
{
    PacketCapacityResultCallback callback;
    const auto *bytes = reinterpret_cast<const uint8_t *>(commands);
    std::memcpy(&callback, bytes + offsetof(PacketBuffer, space_callback), sizeof(callback));
    return callback;
}

inline bool reserve_words_with_result(PacketBuffer *commands, uint32_t requested,
                                      uintptr_t &cursor, uint64_t &rejected_result)
{
    const uintptr_t limit = packet_limit(commands);
    cursor = packet_cursor(commands);
    const uint32_t available = static_cast<uint32_t>((limit - cursor) / packet_word_bytes);
    if (available >= requested) return true;

    const uintptr_t argument = packet_callback_argument(commands);
    const auto callback = packet_capacity_result_callback(commands);
    rejected_result = callback(commands, requested, argument);
    if (static_cast<uint8_t>(rejected_result) == 0) return false;
    cursor = packet_cursor(commands);
    return true;
}
}  // namespace render
