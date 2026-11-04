// SPDX-License-Identifier: GPL-2.0-or-later
// Convert ASCII letters to lowercase in place, stopping at NUL (strlwr-style).
// The caller at 0x01127ca0 uses these byte-string conversions.
#include <cstddef>
#include <cstdint>

namespace {
constexpr unsigned char first_uppercase = 0x41;
constexpr unsigned char last_uppercase = 0x5a;
constexpr unsigned char case_distance = 0x20;
}

extern "C" void *bb_engine_ascii_to_lower_in_place(void *data)
{
    auto *bytes = static_cast<unsigned char *>(data);
    for (std::size_t offset = 0;; ++offset) {
        const unsigned char byte = bytes[offset];
        if (byte == 0) {
            return data;
        }
        if (byte >= first_uppercase && byte <= last_uppercase) {
            bytes[offset] = static_cast<unsigned char>(byte + case_distance);
        }
    }
}
