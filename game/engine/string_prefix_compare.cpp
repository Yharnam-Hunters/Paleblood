// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD's bounded name comparison subtracts signed bytes. It compares the first byte even
// when the signed length is zero or negative, so a standard strncmp is not equivalent.
#include <cstdint>

extern "C" int bb_engine_compare_signed_prefix(const char *left, const char *right, int32_t length)
{
    auto left_bytes = reinterpret_cast<const int8_t *>(left);
    auto right_bytes = reinterpret_cast<const int8_t *>(right);
    int32_t compared = 0;
    do {
        const int left_byte = *left_bytes;
        const int right_byte = *right_bytes;
        if (left_byte == 0 || left_byte != right_byte)
            return left_byte - right_byte;
        ++left_bytes;
        ++right_bytes;
        ++compared;
    } while (compared < length);
    return 0;
}
