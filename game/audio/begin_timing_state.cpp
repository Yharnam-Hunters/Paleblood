// SPDX-License-Identifier: GPL-2.0-or-later
// Start an FMOD timing interval and publish its running byte.
#include <cstdint>

#include "timing_clock.h"
#include "timing_state.h"

extern "C" uint32_t bb_audio_fmod_begin_timing_state(audio::FmodTimingStorage *state)
{
    // Clock failure preserves the start word; this wrapper still starts the interval.
    audio::read_monotonic_microseconds(&state->start_tick);
    state->running = 1;
    return 0;
}
