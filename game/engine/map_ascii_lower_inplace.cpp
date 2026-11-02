// SPDX-License-Identifier: GPL-2.0-or-later
// Only matching ASCII letters receive a store; original logical API is unknown.
#include <cstddef>
#include <cstdint>

namespace {
constexpr unsigned char kFirstUppercase = 0x41;
constexpr unsigned char kLastUppercase = 0x5a;
constexpr unsigned char kCaseDistance = 0x20;
}

extern "C" void *bb_engine_map_ascii_lower_inplace(void *data)
{
    auto *bytes = static_cast<unsigned char *>(data);
    for (std::size_t offset = 0;; ++offset) {
        const unsigned char byte = bytes[offset];
        if (byte == 0) {
            return data;
        }
        if (byte >= kFirstUppercase && byte <= kLastUppercase) {
            bytes[offset] = static_cast<unsigned char>(byte + kCaseDistance);
        }
    }
}
