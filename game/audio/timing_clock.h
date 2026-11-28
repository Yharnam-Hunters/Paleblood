// SPDX-License-Identifier: GPL-2.0-or-later
// Clock service used by the FMOD timing-state consumers.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace audio {

// The established clock writes a DWORD on success and preserves it on error.
RT_ORIGINAL(0x0111a760, read_monotonic_microseconds, int(uint32_t *output));

}  // namespace audio
