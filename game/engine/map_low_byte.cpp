// SPDX-License-Identifier: GPL-2.0-or-later
// Mechanical low-byte mapping; the original API and logical prototype are unknown.
#include <cstdint>

namespace {
enum : uint32_t {
    kUpperBegin = 0x41,
    kUpperEnd = 0x5a,
    kMappingBias = 0x20,
    kByteSignBit = 0x80,
    kSignExtension = 0xffffff00u,
};
}

extern "C" uint64_t bb_engine_map_low_byte(uint64_t value)
{
    uint8_t mapped = static_cast<uint8_t>(value);
    if (mapped >= kUpperBegin && mapped <= kUpperEnd) {
        mapped = static_cast<uint8_t>(mapped + kMappingBias);
    }
    const uint32_t sign_extended = mapped < kByteSignBit ? mapped : kSignExtension | mapped;
    return sign_extended;
}
