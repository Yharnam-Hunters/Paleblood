// SPDX-License-Identifier: GPL-2.0-or-later
// Recover the output context and forward the initialization callback's used inputs.
#include "runtime/original.h"

#include <cstdint>

namespace {
constexpr uintptr_t output_context_offset = 0x38;

RT_ORIGINAL(0x0111b600, audio_fmod_initialize_output,
            uint32_t(void *, uintptr_t, uintptr_t, const uint32_t *, uint32_t,
                     uint32_t *, uintptr_t, uint32_t, uint32_t));
}  // namespace

extern "C" uint32_t bb_audio_fmod_initialize_output_callback(
    void *callback_context, uintptr_t, uintptr_t, const uint32_t *sample_rate,
    uint32_t channel_count, uint32_t *output_word, uintptr_t,
    uint32_t buffer_samples, uint32_t buffer_count)
{
    void *context = nullptr;
    if (callback_context != nullptr) {
        // The wrapper adjusts an opaque address; it never accesses object members.
        const uintptr_t address = reinterpret_cast<uintptr_t>(callback_context);
        context = reinterpret_cast<void *>(address - output_context_offset);
    }
    // The callee ignores the second, third and seventh argument positions.
    return audio_fmod_initialize_output(context, 0, 0, sample_rate, channel_count,
                                        output_word, 0, buffer_samples, buffer_count);
}
