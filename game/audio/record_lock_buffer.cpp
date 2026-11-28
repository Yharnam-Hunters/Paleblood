// SPDX-License-Identifier: GPL-2.0-or-later
// Describe the locked part of an FMOD recording buffer as one or two regions.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
struct RecordDescriptor {
    std::byte unknown_0x0000[0x18];
    int32_t port_index;
};
static_assert(offsetof(RecordDescriptor, port_index) == 0x18);

// Only the starts of the two indexed tables are known here.
struct RecordingOwner {
    std::byte unknown_0x0000[0x2d0];
    uintptr_t first_capture_buffer;
    std::byte unknown_0x02d8[0x1f8];
    uint32_t first_capacity;
};
static_assert(offsetof(RecordingOwner, first_capture_buffer) == 0x2d0);
static_assert(offsetof(RecordingOwner, first_capacity) == 0x4d0);

template <class T>
T read_port_entry(uintptr_t table, int32_t index)
{
    T value;
    const uintptr_t address = table + static_cast<uintptr_t>(index) * sizeof(T);
    std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
    return value;
}

template <class T>
void write_output(void *destination, T value)
{
    std::memcpy(destination, &value, sizeof(value));
}
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_lock_buffer(void *callback_context,
    const RecordDescriptor *record, uint32_t offset, uint32_t length, void *first_pointer,
    void *second_pointer, void *first_length, void *second_length)
{
    constexpr uintptr_t callback_context_offset = 0x38;
    const uintptr_t owner = callback_context == nullptr ? 0 :
        reinterpret_cast<uintptr_t>(callback_context) - callback_context_offset;
    const uintptr_t buffers = owner + offsetof(RecordingOwner, first_capture_buffer);
    const int32_t index = record->port_index;
    const uint32_t capacity = read_port_entry<uint32_t>(
        owner + offsetof(RecordingOwner, first_capacity), index);
    if (offset >= capacity) {
        write_output(second_pointer, uintptr_t{0});
        write_output(first_pointer, uintptr_t{0});
        write_output(second_length, uint32_t{0});
        write_output(first_length, uint32_t{0});
        return 0x25;
    }

    if (length > capacity)
        length = capacity;
    write_output(first_pointer, read_port_entry<uintptr_t>(buffers, index) + offset);
    // The original tests the 32-bit sum and publishes the table base on wrap.
    if (offset + length > capacity) {
        const uint32_t available = capacity - offset;
        write_output(first_length, available);
        write_output(second_pointer, buffers);
        write_output(second_length, length - available);
    } else {
        write_output(first_length, length);
        write_output(second_pointer, uintptr_t{0});
        write_output(second_length, uint32_t{0});
    }
    return 0;
}
