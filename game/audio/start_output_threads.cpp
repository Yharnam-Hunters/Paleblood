// SPDX-License-Identifier: GPL-2.0-or-later
// Start the mixer thread, then AudioOut only after successful mixer setup.
#include "output_threads.h"
#include "runtime/original.h"

namespace {
constexpr uintptr_t callback_context_bias = 0x38;
constexpr int32_t mixer_priority_mode = 2;
constexpr int32_t audio_out_priority_mode = 3;
constexpr int32_t mixer_stack_bytes = 0xc000;
constexpr uint8_t mixer_needs_wakeup = 1;
constexpr std::size_t mixer_name_bytes = 18;
constexpr std::size_t audio_out_name_bytes = 21;

using MixerName = const char[mixer_name_bytes];
using AudioOutName = const char[audio_out_name_bytes];
RT_GLOBAL(0x04c7d1d2, mixer_name, MixerName);
RT_GLOBAL(0x04c7d1e4, audio_out_name, AudioOutName);
RT_ORIGINAL(0x0111b910, mixer_entry, uint32_t(void *));
RT_ORIGINAL(0x0111b920, audio_out_entry, uint32_t(void *));
RT_ORIGINAL(0x01118420, initialize_fmod_thread,
            uint32_t(audio::FmodThreadStorage *, const char *, void *, void *,
                     int32_t, uintptr_t, int32_t, uint8_t, uint32_t, void *));
}  // namespace

extern "C" uint32_t bb_audio_fmod_start_output_threads(void *callback_context)
{
    const uintptr_t owner_address = callback_context == nullptr ? 0 :
        reinterpret_cast<uintptr_t>(callback_context) - callback_context_bias;
    auto *owner = reinterpret_cast<audio::OutputThreadContext *>(owner_address);

    // The original requires readable owner storage, including the service pointer.
    const uint32_t result = initialize_fmod_thread(
        &owner->mixer_thread, mixer_name.get(), mixer_entry.address(), owner,
        mixer_priority_mode, 0, mixer_stack_bytes, mixer_needs_wakeup, 0,
        owner->service_context);
    if (result != 0)
        return result;

    // Thread startup and its notification may change the owner's service context.
    return initialize_fmod_thread(
        &owner->audio_out_thread, audio_out_name.get(), audio_out_entry.address(), owner,
        audio_out_priority_mode, 0, 0, 0, 0, owner->service_context);
}
