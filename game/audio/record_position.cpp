// SPDX-License-Identifier: GPL-2.0-or-later
// Convert the indexed recording position using the descriptor's format and divisor.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {
struct RecordingContext {
    std::byte unknown_0x0000[0x418];
    int32_t first_position;
};
static_assert(offsetof(RecordingContext, first_position) == 0x418);

struct RecordDescriptor {
    std::byte unknown_0x0000[0x18];
    int32_t port_index;
    std::byte unknown_0x001c[0x20];
    uint32_t format;
    std::byte unknown_0x0040[0x14];
    uint32_t divisor;
};
static_assert(offsetof(RecordDescriptor, port_index) == 0x18);
static_assert(offsetof(RecordDescriptor, format) == 0x3c);
static_assert(offsetof(RecordDescriptor, divisor) == 0x54);
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_position(const RecordingContext *context,
    const RecordDescriptor *record, void *output)
{
    const uintptr_t table = reinterpret_cast<uintptr_t>(&context->first_position);
    const uintptr_t address = table + static_cast<uintptr_t>(record->port_index) * sizeof(int32_t);
    int32_t raw_position;
    std::memcpy(&raw_position, reinterpret_cast<const void *>(address), sizeof(raw_position));
    const uint32_t position = raw_position < 0 ? 0 : static_cast<uint32_t>(raw_position);
    const uint32_t divisor = record->divisor;
    if (divisor == 0)
        return 0;

    // Formats below the direct-copy range convert byte positions to units. The
    // packed formats multiply in 32 bits, including wraparound at large positions.
    constexpr uint32_t units_per_block[] = {0, 1, 1, 1, 1, 1, 14, 64, 28, 28};
    constexpr uint32_t bytes_per_block[] = {1, 1, 2, 3, 4, 4, 8, 36, 16, 16};
    static_assert(std::size(units_per_block) == std::size(bytes_per_block));
    constexpr uint32_t skipped_format = 14;
    constexpr uint32_t last_format = 15;
    const uint32_t format = record->format;
    if (format == skipped_format || format > last_format)
        return 0;

    uint32_t result = position;
    if (format < std::size(units_per_block)) {
        const uint32_t units = position * units_per_block[format];
        result = units / bytes_per_block[format] / divisor;
    }
    std::memcpy(output, &result, sizeof(result));
    return 0;
}
