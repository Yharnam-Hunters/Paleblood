// SPDX-License-Identifier: GPL-2.0-or-later
// Copy the fixed output label and terminate the requested extent after the copy.
#include "runtime/original.h"

#include <cstddef>
#include <cstdint>

namespace {
constexpr std::size_t output_label_bytes = 22;
using OutputLabel = const char[output_label_bytes];

RT_GLOBAL(0x04c7d175, orbis_output_label, OutputLabel);
RT_ORIGINAL(0x011c4840, audio_fmod_string_copy_counted,
            char *(char *, const char *, uint32_t));
}  // namespace

// The two leading argument positions are unused by this callback.
extern "C" uint64_t bb_audio_fmod_copy_output_label(uintptr_t, uintptr_t,
                                                  char *destination, int32_t count)
{
    if (destination != nullptr && count > 0) {
        const int32_t last = count - 1;
        audio_fmod_string_copy_counted(destination, orbis_output_label.get(),
                                       static_cast<uint32_t>(last));
        // The counted helper may stop early. Publish the far terminator afterward.
        destination[last] = '\0';
    }
    return 0;
}
