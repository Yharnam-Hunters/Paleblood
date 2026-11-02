// SPDX-License-Identifier: GPL-2.0-or-later
// Capture both bytes before the zero test; signed BYTE subtraction writes EAX.
#include <cstddef>
#include <cstdint>

namespace {
constexpr unsigned char kFirstUppercase = 0x41;
constexpr unsigned char kLastUppercase = 0x5a;
constexpr unsigned char kCaseDistance = 0x20;
constexpr int32_t kByteSignBit = 0x80;
constexpr int32_t kByteModulus = 0x100;
}

extern "C" uint64_t bb_engine_compare_ascii_folded_signed_bytes(const void *left, const void *right)
{
    const auto *left_bytes = static_cast<const unsigned char *>(left);
    const auto *right_bytes = static_cast<const unsigned char *>(right);
    for (std::size_t offset = 0;; ++offset) {
        unsigned char left_byte = left_bytes[offset];
        if (left_byte >= kFirstUppercase && left_byte <= kLastUppercase) {
            left_byte = static_cast<unsigned char>(left_byte + kCaseDistance);
        }
        unsigned char right_byte = right_bytes[offset];
        if (right_byte >= kFirstUppercase && right_byte <= kLastUppercase) {
            right_byte = static_cast<unsigned char>(right_byte + kCaseDistance);
        }
        if (left_byte == 0 || left_byte != right_byte) {
            const int32_t signed_left = left_byte < kByteSignBit ? left_byte : static_cast<int32_t>(left_byte) - kByteModulus;
            const int32_t signed_right = right_byte < kByteSignBit ? right_byte : static_cast<int32_t>(right_byte) - kByteModulus;
            const int32_t difference = signed_left - signed_right;
            return static_cast<uint32_t>(difference);
        }
    }
}
