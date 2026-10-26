// SPDX-License-Identifier: GPL-2.0-or-later
// Mechanical byte scan; the observed EAX subtraction gives length modulo 2^32.
#include <cstddef>
#include <cstdint>

extern "C" uint64_t bb_engine_byte_length_modulo32(const void *data)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t offset = 0;
    while (bytes[offset] != 0) {
        ++offset;
    }
    return static_cast<uint32_t>(offset);
}
