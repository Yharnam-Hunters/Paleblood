// SPDX-License-Identifier: GPL-2.0-or-later
// These writers return the callback's full result when its low byte declines space.
#pragma once

#include "command_reservation.h"

namespace render {
using PacketCapacityResultCallback = uint64_t (*)(PacketBuffer *, uint32_t requested_words,
                                                 uintptr_t argument);
static_assert(sizeof(PacketCapacityResultCallback) == sizeof(PacketSpaceCallback));

}  // namespace render
