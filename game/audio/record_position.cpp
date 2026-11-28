// SPDX-License-Identifier: GPL-2.0-or-later
// Convert the indexed recording position using the descriptor's format and divisor.
#include <cstddef>
#include <cstdint>
#include <cstring>

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

    uint32_t scaled;
    switch (record->format) {
    case 0: scaled = 0; break;
    case 1: scaled = position; break;
    case 2: scaled = position / 2; break;
    case 3: scaled = position / 3; break;
    case 4:
    case 5: scaled = position / 4; break;
    // These formats truncate the product to 32 bits before the first division.
    case 6: scaled = (position * 14) / 8; break;
    case 7: scaled = (position * 64) / 36; break;
    case 8:
    case 9: scaled = (position * 28) / 16; break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 15:
        std::memcpy(output, &position, sizeof(position));
        return 0;
    default:
        return 0;
    }

    const uint32_t result = scaled / divisor;
    std::memcpy(output, &result, sizeof(result));
    return 0;
}
