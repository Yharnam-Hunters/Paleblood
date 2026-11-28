// SPDX-License-Identifier: GPL-2.0-or-later
// Forward the output pointer to the engine's current-thread helper. The first
// argument is unused and the adapter always reports success.
#include <cstdint>

#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x0111a8a0, kernel_thread_self, int(std::uintptr_t *output));
}

extern "C" int bb_audio_fmod_current_thread_handle(void *, std::uintptr_t *output)
{
    kernel_thread_self(output);
    return 0;
}
