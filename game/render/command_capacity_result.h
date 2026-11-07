// SPDX-License-Identifier: GPL-2.0-or-later
// These writers return the callback's full result when its low byte declines space.
#pragma once

#include "command_reservation.h"

namespace render {
using PacketCapacityResultCallback = uint64_t (*)(PacketBuffer *, uint32_t requested_words,
                                                 uintptr_t argument);
static_assert(sizeof(PacketCapacityResultCallback) == sizeof(PacketSpaceCallback));

inline bool reserve_words_with_result(PacketBuffer *commands, uint32_t requested,
                                      uintptr_t &cursor, uint64_t &rejected_result)
{
    return reserve_packet_words<PacketCapacityResultCallback>(commands, requested, cursor,
                                                              rejected_result);
}
}  // namespace render
