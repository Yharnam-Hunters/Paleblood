// SPDX-License-Identifier: GPL-2.0-or-later
// NUL-terminated string copy used by FMOD callers; copy the terminator and return destination.
// Observed callers lie in the FMOD code range 0x0110 through 0x0121.
#include <cstddef>

extern "C" void *bb_audio_fmod_string_copy(void *destination, const void *source)
{
    auto *destination_bytes = static_cast<unsigned char *>(destination);
    const auto *source_bytes = static_cast<const unsigned char *>(source);
    for (std::size_t offset = 0;; ++offset) {
        const unsigned char byte = source_bytes[offset];
        destination_bytes[offset] = byte;
        if (byte == 0) {
            return destination;
        }
    }
}
