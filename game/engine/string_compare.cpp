// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's byte-string comparator subtracts signed bytes. FMOD's thread-name lookup
// uses it for exact names; bytes above ASCII therefore sort before ordinary positive bytes.
#include <cstdint>

extern "C" int bb_engine_compare_signed_strings(const char *left, const char *right)
{
    auto left_bytes = reinterpret_cast<const int8_t *>(left);
    auto right_bytes = reinterpret_cast<const int8_t *>(right);
    for (;;) {
        const int left_byte = *left_bytes;
        const int right_byte = *right_bytes;
        if (left_byte == 0 || left_byte != right_byte)
            return left_byte - right_byte;
        ++left_bytes;
        ++right_bytes;
    }
}
