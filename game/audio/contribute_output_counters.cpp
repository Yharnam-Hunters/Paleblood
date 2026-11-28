// SPDX-License-Identifier: GPL-2.0-or-later
// Contribute the output owner's counters and pass the request on to its children.
#include "counter_contribution.h"
#include "output_ports.h"

namespace {
constexpr uintptr_t callback_context_bias = 0x38;
// The counter helper's observed output-owner classification.
constexpr uint32_t output_classification = 16;
static_assert(sizeof(audio::OutputPortContext) == 0x988);

RT_ORIGINAL(0x011b0d40, contribute_output_children,
            uint32_t(audio::OutputPortContext *, audio::CounterContext *));
}  // namespace

extern "C" uint32_t bb_audio_fmod_contribute_output_counters(
    void *callback_context, audio::CounterContext *counters)
{
    const uintptr_t owner_address = callback_context == nullptr ? 0 :
        reinterpret_cast<uintptr_t>(callback_context) - callback_context_bias;
    auto *owner = reinterpret_cast<audio::OutputPortContext *>(owner_address);
    if (counters != nullptr && owner->counters_registered != 0)
        return 0;

    audio::add_counter_contribution(counters, 0, output_classification, sizeof(*owner));
    // The first contribution can overlap the mix buffer pointer or its count.
    if (owner->mix_buffer != nullptr)
        audio::add_counter_contribution(counters, 0, output_classification, owner->mix_buffer_contribution);

    const uint32_t result = contribute_output_children(owner, counters);
    if (result != 0)
        return result;
    owner->counters_registered = counters != nullptr;
    return 0;
}
