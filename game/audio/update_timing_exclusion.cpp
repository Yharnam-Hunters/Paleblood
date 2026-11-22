// SPDX-License-Identifier: GPL-2.0-or-later
// Update nested exclusion intervals in the accessed FMOD timing-state prefix.
#include "timing_clock.h"
#include "timing_state.h"

extern "C" uint32_t bb_audio_fmod_update_timing_exclusion(audio::FmodTimingStorage *state,
                                                        uint8_t excluding)
{
    if (state->running == 0)
        return 0;

    if (excluding != 0) {
        if (state->exclusion_depth == 0)
            audio::read_monotonic_microseconds(&state->exclusion_start_tick);
        // The clock can update aliased storage, including the nesting depth.
        ++state->exclusion_depth;
    } else {
        --state->exclusion_depth;
        if (state->exclusion_depth == 0) {
            audio::read_monotonic_microseconds(&state->exclusion_end_tick);
            const uint32_t start = state->exclusion_start_tick;
            const uint32_t end = state->exclusion_end_tick;
            if (end > start)
                state->excluded_ticks += end - start;
        }
    }
    // Publish the original low byte after the clock, including on clock failure.
    state->excluding = excluding;
    return 0;
}
