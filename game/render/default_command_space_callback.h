// SPDX-License-Identifier: GPL-2.0-or-later
// The default callback declines capacity requests. Initialization installs its
// original entry when the caller supplies no callback.
#pragma once

#include "command_submit.h"

namespace render {
RT_ORIGINAL(0x014712b0, original_default_command_space_callback,
            uint8_t(PacketBuffer *, uint32_t requested_words, uintptr_t argument));

inline PacketSpaceCallback original_default_command_space_callback_address()
{
    return reinterpret_cast<PacketSpaceCallback>(original_default_command_space_callback.address());
}
}  // namespace render
