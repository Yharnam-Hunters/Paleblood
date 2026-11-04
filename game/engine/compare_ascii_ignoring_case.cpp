// SPDX-License-Identifier: GPL-2.0-or-later
// Compare NUL-terminated strings without ASCII case differences (stricmp-style).
// Capture both bytes before the left NUL test; return their signed-byte difference.
#include <cstddef>
#include <cstdint>

namespace {
constexpr unsigned char first_uppercase = 0x41;
constexpr unsigned char last_uppercase = 0x5a;
constexpr unsigned char case_distance = 0x20;
}

extern "C" int32_t bb_engine_compare_ascii_ignoring_case(const void *left, const void *right)
{
    const auto *left_bytes = static_cast<const unsigned char *>(left);
    const auto *right_bytes = static_cast<const unsigned char *>(right);
    for (std::size_t offset = 0;; ++offset) {
        unsigned char left_byte = left_bytes[offset];
        if (left_byte >= first_uppercase && left_byte <= last_uppercase) {
            left_byte = static_cast<unsigned char>(left_byte + case_distance);
        }
        unsigned char right_byte = right_bytes[offset];
        if (right_byte >= first_uppercase && right_byte <= last_uppercase) {
            right_byte = static_cast<unsigned char>(right_byte + case_distance);
        }
        if (left_byte == 0 || left_byte != right_byte) {
            return static_cast<int8_t>(left_byte) - static_cast<int8_t>(right_byte);
        }
    }
}
