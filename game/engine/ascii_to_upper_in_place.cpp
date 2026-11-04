// SPDX-License-Identifier: GPL-2.0-or-later
// Convert ASCII letters to uppercase in place, stopping at NUL (strupr-style).
// The caller at 0x01127ca0 uses these byte-string conversions.
#include <cstddef>
#include <cstdint>

namespace {
constexpr unsigned char first_lowercase = 0x61;
constexpr unsigned char last_lowercase = 0x7a;
constexpr unsigned char case_distance = 0x20;
}

extern "C" void *bb_engine_ascii_to_upper_in_place(void *data)
{
    auto *bytes = static_cast<unsigned char *>(data);
    for (std::size_t offset = 0;; ++offset) {
        const unsigned char byte = bytes[offset];
        if (byte == 0) {
            return data;
        }
        if (byte >= first_lowercase && byte <= last_lowercase) {
            bytes[offset] = static_cast<unsigned char>(byte - case_distance);
        }
    }
}
