// SPDX-License-Identifier: GPL-2.0-or-later
// Mechanical forward byte copy; no original logical API or prototype is inferred.
#include <cstddef>

extern "C" void *bb_engine_copy_bytes_until_zero(void *destination, const void *source)
{
    auto *destination_bytes = static_cast<unsigned char *>(destination);
    const auto *source_bytes = static_cast<const unsigned char *>(source);
    for (std::size_t offset = 0;; ++offset) {
        const unsigned char byte = source_bytes[offset];
        destination_bytes[offset] = byte;
        if (byte == 0) {
            return destination;
        }
    }
}
