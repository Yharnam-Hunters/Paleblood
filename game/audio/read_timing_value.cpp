// SPDX-License-Identifier: GPL-2.0-or-later
// Read the cached scalar in the accessed FMOD timing-storage prefix.
#include "timing_state.h"

#include <cstring>

namespace {

constexpr uint32_t missing_output_status = 0x25;

}  // namespace

extern "C" uint32_t bb_audio_fmod_read_timing_value(const audio::FmodTimingStorage *state,
                                                 float *output)
{
    if (output == nullptr)
        return missing_output_status;
    // Preserve the cached word's raw bits, including overlapping output ranges.
    std::memmove(output, &state->cached_value_bits, sizeof(*output));
    return 0;
}
