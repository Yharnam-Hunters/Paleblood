// SPDX-License-Identifier: GPL-2.0-or-later
// Stop AudioOut before the mixer, preserving each Thread helper's failure.
#include "output_threads.h"
#include "runtime/original.h"

namespace {
constexpr uintptr_t callback_context_bias = 0x38;
RT_ORIGINAL(0x011185a0, stop_fmod_thread, uint32_t(audio::FmodThreadStorage *));
}  // namespace

extern "C" uint32_t bb_audio_fmod_stop_output_threads(void *callback_context)
{
    const uintptr_t owner_address = callback_context == nullptr ? 0 :
        reinterpret_cast<uintptr_t>(callback_context) - callback_context_bias;
    auto *owner = reinterpret_cast<audio::OutputThreadContext *>(owner_address);

    // The original requires readable Thread storage, including on its first call.
    const uint32_t result = stop_fmod_thread(&owner->audio_out_thread);
    if (result != 0)
        return result;

    return stop_fmod_thread(&owner->mixer_thread);
}
