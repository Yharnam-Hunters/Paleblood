// SPDX-License-Identifier: GPL-2.0-or-later
// Select a logged-in user's input, allocate a capture ring and start the first recording thread.
#include <array>
#include <cstring>

#include "output_device_calls.h"
#include "output_threads.h"
#include "record_login_call.h"
#include "start_thread_call.h"
#include "thread_services.h"

namespace {
struct RecordDescriptor {
    std::byte unknown_0x0000[0x18];
    int32_t port_index;
    std::byte unknown_0x001c[0x20];
    uint32_t format;
    std::byte unknown_0x0040[4];
    uint32_t sample_count;
    std::byte unknown_0x0048[8];
    uint32_t frequency;
    uint32_t position_divisor;
};
static_assert(offsetof(RecordDescriptor, port_index) == 0x18);
static_assert(offsetof(RecordDescriptor, format) == 0x3c);
static_assert(offsetof(RecordDescriptor, sample_count) == 0x44);
static_assert(offsetof(RecordDescriptor, frequency) == 0x50);
static_assert(offsetof(RecordDescriptor, position_divisor) == 0x54);

struct RecordingOwner {
    std::byte unknown_0x0000[0x60];
    void *service_context;
    std::byte unknown_0x0068[0x1e4];
    int32_t first_input_handle;
    std::byte unknown_0x0250[0x80];
    void *first_capture_buffer;
    std::byte unknown_0x02d8[0xf8];
    uint32_t first_write_offset;
    std::byte unknown_0x03d4[0x7c];
    int32_t first_position;
    std::byte unknown_0x0454[0x7c];
    uint32_t first_capacity;
    std::byte unknown_0x04d4[0x7c];
    audio::FmodThreadStorage capture_thread;
    uint32_t active_port_count;
};
static_assert(offsetof(RecordingOwner, service_context) == 0x60);
static_assert(offsetof(RecordingOwner, first_input_handle) == 0x24c);
static_assert(offsetof(RecordingOwner, first_capture_buffer) == 0x2d0);
static_assert(offsetof(RecordingOwner, first_write_offset) == 0x3d0);
static_assert(offsetof(RecordingOwner, first_position) == 0x450);
static_assert(offsetof(RecordingOwner, first_capacity) == 0x4d0);
static_assert(offsetof(RecordingOwner, capture_thread) == 0x550);
static_assert(offsetof(RecordingOwner, active_port_count) == 0x6a8);

RT_ORIGINAL(0x02fbffa8, sce_audio_in_open,
            int32_t(int32_t user, uint32_t type, uint32_t index, uint32_t samples,
                    uint32_t frequency, uint32_t format));
RT_ORIGINAL(0x011aaea0, allocate_record_buffer,
            void *(audio::MemoryPool *, uint32_t bytes, const char *source_file, int32_t source_line,
                   uint32_t flags));
RT_ORIGINAL(0x0111be80, capture_thread_entry, void(void *));
RT_GLOBAL(0x04c7d221, capture_thread_name, const char);

constexpr std::size_t login_user_slots = 4;
constexpr int32_t inputs_per_user = 3;
constexpr int32_t invalid_user = -1;
constexpr uint32_t voice_chat = 0, general = 1, voice_recognition = 5;
constexpr uint32_t input_grain_samples = 256, sample_rate = 16000, s16_mono = 0;
constexpr uint32_t pcm16_format = 2, capture_samples = 1536;
constexpr uint32_t capture_bytes = capture_samples * sizeof(int16_t);
constexpr int32_t initial_position = -512;
constexpr int32_t allocation_source_line = 762;
constexpr int32_t recording_priority = 1, recording_stack_bytes = 0x4000;
constexpr uint32_t login_uninitialized = 0x80960002u, service_unavailable = 0x3a;
constexpr uint32_t login_error = 0x21, missing_driver = 0x25;
constexpr uint32_t open_error = 0x3c, allocation_error = 0x2b;

// Only each table's first slot is known. Negative port indices address preceding storage.
template <class T>
void *port_entry(T *first, int32_t index)
{
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(first)
        + static_cast<uintptr_t>(index) * sizeof(T));
}

template <class T>
void set_entry(T *first, int32_t index, T value)
{
    std::memcpy(port_entry(first, index), &value, sizeof(value));
}
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_open_port(RecordingOwner *owner, RecordDescriptor *record)
{
    int32_t driver = record->port_index;
    std::array<int32_t, login_user_slots> users;
    const int32_t login = audio::sce_user_service_get_login_user_id_list(users.data());
    if (static_cast<uint32_t>(login) == login_uninitialized)
        return service_unavailable;
    if (login < 0)
        return login_error;

    int32_t selected_user = invalid_user;
    for (int32_t user : users) {
        if (user != invalid_user) {
            if (driver < inputs_per_user) {
                selected_user = user;
                break;
            }
            driver -= inputs_per_user;
        }
    }
    if (selected_user == invalid_user)
        return missing_driver;

    constexpr int32_t general_driver = 1, recognition_driver = 2;
    const uint32_t type = driver == recognition_driver ? voice_recognition :
                          driver == general_driver ? general : voice_chat;
    set_entry(&owner->first_write_offset, record->port_index, uint32_t{0});
    set_entry(&owner->first_position, record->port_index, initial_position);
    const int32_t handle = sce_audio_in_open(selected_user, type, 0, input_grain_samples,
                                              sample_rate, s16_mono);
    set_entry(&owner->first_input_handle, record->port_index, handle);
    // The published handle may itself overlap the descriptor's port index.
    int32_t selected_handle;
    std::memcpy(&selected_handle, port_entry(&owner->first_input_handle, record->port_index),
                sizeof(selected_handle));
    if (selected_handle < 0)
        return open_error;

    record->position_divisor = 1;
    record->frequency = sample_rate;
    record->format = pcm16_format;
    record->sample_count = capture_samples;
    void *buffer = allocate_record_buffer(audio::thread_services.get()->memory_pool, capture_bytes,
                                          audio::output_source_file.address(), allocation_source_line, 0);
    const int32_t port = record->port_index;
    set_entry(&owner->first_capture_buffer, port, buffer);
    if (buffer == nullptr)
        return allocation_error;
    set_entry(&owner->first_capacity, port, capture_bytes);

    if (owner->active_port_count == 0) {
        const uint32_t status = audio::initialize_fmod_thread(&owner->capture_thread, capture_thread_name.address(),
            capture_thread_entry.address(), owner, recording_priority, 0, recording_stack_bytes, 0, 0,
            owner->service_context);
        if (status != 0)
            return status;
    }
    ++owner->active_port_count;
    return 0;
}
