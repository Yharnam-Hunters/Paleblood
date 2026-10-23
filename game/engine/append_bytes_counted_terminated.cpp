// SPDX-License-Identifier: GPL-2.0-or-later
// Low32 zero bypasses every access; exhaustion appends one extra zero byte.
#include <cstddef>
#include <cstdint>

extern "C" void *bb_engine_append_bytes_counted_terminated(void *destination, const void *source,
                                                         uint64_t count_input)
{
    const uint32_t count = static_cast<uint32_t>(count_input);
    if (count == 0) {
        return destination;
    }
    auto *destination_bytes = static_cast<unsigned char *>(destination);
    const auto *source_bytes = static_cast<const unsigned char *>(source);
    std::size_t terminal = 0;
    while (destination_bytes[terminal] != 0) {
        ++terminal;
    }
    for (std::size_t offset = 0; offset < count; ++offset) {
        const unsigned char byte = source_bytes[offset];
        destination_bytes[terminal + offset] = byte;
        if (byte == 0) {
            return destination;
        }
    }
    destination_bytes[terminal + count] = 0;
    return destination;
}
