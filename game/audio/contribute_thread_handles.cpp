// SPDX-License-Identifier: GPL-2.0-or-later
// Contribute counter values for the optional FMOD Thread semaphore handles.
#include <cstdint>

#include "output_threads.h"
#include "runtime/original.h"

namespace {

struct CounterContext;

RT_ORIGINAL(0x011abd10, add_counter_contribution,
            void(CounterContext *counters, uint8_t bank, uint32_t classification, uint32_t count));
RT_GLOBAL(0x05519b0c, handle_contribution, uint32_t);

// The observed classification for these contributions; its original category name is unknown.
constexpr uint32_t thread_handle_classification = 4;

}  // namespace

extern "C" uint32_t bb_audio_fmod_contribute_thread_handles(
    audio::FmodThreadStorage *thread, CounterContext *counters)
{
    if (thread->wakeup_semaphore)
        add_counter_contribution(counters, 0, thread_handle_classification, handle_contribution.get());

    // An overlapping counter object can change the next handle or the shared contribution.
    if (thread->startup_semaphore)
        add_counter_contribution(counters, 0, thread_handle_classification, handle_contribution.get());
    return 0;
}
