// SPDX-License-Identifier: GPL-2.0-or-later
// Read the cached scalar in the accessed FMOD timing-storage prefix.
#include "timing_state.h"

#include <cstring>

namespace {

constexpr uint32_t missing_output_status = 0x25;
constexpr size_t scalar_offset = 0x24;

}  // namespace

extern "C" uint32_t bb_audio_fmod_read_timing_value(const audio::FmodTimingStorage *state,
                                                 float *output)
{
    if (output == nullptr)
        return missing_output_status;
    // The scalar occupies bytes 0x24-0x27. Preserve its bits and overlapping ranges.
    const auto *scalar = reinterpret_cast<const std::byte *>(state) + scalar_offset;
    std::memmove(output, scalar, sizeof(*output));
    return 0;
}
