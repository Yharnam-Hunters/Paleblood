// SPDX-License-Identifier: GPL-2.0-or-later
// Scan first, then capture each source byte before its forward destination store.
#include <cstddef>
#include <cstdint>

extern "C" void *bb_engine_append_bytes_until_zero(void *destination, const void *source)
{
    auto *destination_bytes = static_cast<unsigned char *>(destination);
    const auto *source_bytes = static_cast<const unsigned char *>(source);
    std::size_t terminal = 0;
    while (destination_bytes[terminal] != 0) {
        ++terminal;
    }
    for (std::size_t offset = 0;; ++offset) {
        const unsigned char byte = source_bytes[offset];
        destination_bytes[terminal + offset] = byte;
        if (byte == 0) {
            return destination;
        }
    }
}
