// SPDX-License-Identifier: GPL-2.0-or-later
// Mechanical forward byte copy; count is captured from EDX and no padding is added.
#include <cstddef>
#include <cstdint>

extern "C" void *bb_engine_copy_bytes_counted(void *destination, const void *source,
                                             uint64_t count_input)
{
    const uint32_t count = static_cast<uint32_t>(count_input);
    auto *destination_bytes = static_cast<unsigned char *>(destination);
    const auto *source_bytes = static_cast<const unsigned char *>(source);
    for (std::size_t offset = 0; offset < count; ++offset) {
        const unsigned char byte = source_bytes[offset];
        destination_bytes[offset] = byte;
        if (byte == 0) {
            break;
        }
    }
    return destination;
}
