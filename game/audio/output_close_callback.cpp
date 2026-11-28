// SPDX-License-Identifier: GPL-2.0-or-later
// Recover the FMOD output owner and forward its port-close result.
#include "output_ports.h"
#include "runtime/original.h"

namespace {
constexpr uintptr_t callback_context_offset = 0x38;
RT_ORIGINAL(0x0111b7c0, audio_fmod_output_close, uint32_t(audio::OutputPortContext *owner));
}  // namespace

extern "C" uint32_t bb_audio_fmod_close_output_ports(void *callback_context)
{
    audio::OutputPortContext *owner = nullptr;
    if (callback_context != nullptr) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(callback_context);
        owner = reinterpret_cast<audio::OutputPortContext *>(address - callback_context_offset);
    }
    return audio_fmod_output_close(owner);
}
