// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "runtime/original.h"

namespace audio {
struct FmodThreadStorage;

RT_ORIGINAL(0x01118420, initialize_fmod_thread,
            uint32_t(FmodThreadStorage *, const char *, void *, void *, int32_t, uintptr_t,
                     int32_t, uint8_t, uint32_t, void *));
}  // namespace audio
