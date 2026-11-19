// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize FMOD clock bookkeeping without clearing accumulated/output fields.
#include "timing_state.h"

// Observed direct callers discard the result; the original defines no result bits.
extern "C" void bb_audio_fmod_initialize_timing_state(audio::FmodTimingStorage *state)
{
    state->excluding = 0;
    state->excluded_ticks = 0;
    state->exclusion_depth = 0;
    state->running = 0;
    state->previous_sample_tick = 0;
    state->sample_tick = 0;
    state->exclusion_start_tick = 0;
    state->exclusion_end_tick = 0;
    state->start_tick = 0;
    state->end_tick = 0;
}
