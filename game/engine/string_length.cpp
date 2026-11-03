// SPDX-License-Identifier: GPL-2.0-or-later
// Engine strlen: count bytes before the first NUL and return the low 32 bits.
// The original subtracts addresses in EAX, so RAX contains a zero-extended length.
#include <cstddef>
#include <cstdint>

extern "C" uint64_t bb_engine_string_length(const void *data)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t offset = 0;
    while (bytes[offset] != 0) {
        ++offset;
    }
    return static_cast<uint32_t>(offset);
}
