// SPDX-License-Identifier: GPL-2.0-or-later
// Close an FMOD recording port and release the selected capture buffer.
#include <cstring>

#include "output_device_calls.h"
#include "output_threads.h"
#include "thread_services.h"

namespace {
struct RecordDescriptor {
    std::byte unknown_0x0000[0x18];
    int32_t port_index;
};
static_assert(offsetof(RecordDescriptor, port_index) == 0x18);

struct RecordingOwner {
    std::byte unknown_0x0000[0x24c];
    int32_t first_input_handle;
    std::byte unknown_0x0250[0x80];
    void *first_capture_buffer;
    std::byte unknown_0x02d8[0x278];
    audio::FmodThreadStorage capture_thread;
    uint32_t active_port_count;
};
static_assert(offsetof(RecordingOwner, first_input_handle) == 0x24c);
static_assert(offsetof(RecordingOwner, first_capture_buffer) == 0x2d0);
static_assert(offsetof(RecordingOwner, capture_thread) == 0x550);
static_assert(offsetof(RecordingOwner, active_port_count) == 0x6a8);

RT_ORIGINAL(0x02fbffb8, sce_audio_in_close, int32_t(int32_t handle));
constexpr int32_t original_close_source_line = 0x339;

// Only the table starts are known. The executable sign-extends the port index.
template <class T>
void *port_entry(T *first, int32_t index)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(first);
    return reinterpret_cast<void *>(base + static_cast<uintptr_t>(index) * sizeof(T));
}
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_close_port(RecordingOwner *owner,
                                                    const RecordDescriptor *record)
{
    int32_t handle;
    std::memcpy(&handle, port_entry(&owner->first_input_handle, record->port_index), sizeof(handle));
    sce_audio_in_close(handle);
    if (--owner->active_port_count == 0) {
        const uint32_t status = audio::stop_fmod_thread(&owner->capture_thread);
        if (status != 0)
            return status;
    }

    void *buffer;
    std::memcpy(&buffer, port_entry(&owner->first_capture_buffer, record->port_index), sizeof(buffer));
    if (buffer != nullptr) {
        audio::release_thread_storage(audio::thread_services.get()->memory_pool, buffer,
                                      audio::output_source_file.address(), original_close_source_line);
        // The release can select another port before the slot is cleared.
        buffer = nullptr;
        std::memcpy(port_entry(&owner->first_capture_buffer, record->port_index), &buffer, sizeof(buffer));
    }
    return 0;
}
