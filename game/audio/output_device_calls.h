// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "runtime/original.h"

namespace audio {
struct FmodThreadStorage;

RT_ORIGINAL(0x011185a0, stop_fmod_thread, uint32_t(FmodThreadStorage *));
RT_GLOBAL(0x04c7d18b, output_source_file, const char);
}  // namespace audio
