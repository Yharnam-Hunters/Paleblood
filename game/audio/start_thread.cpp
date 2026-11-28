// SPDX-License-Identifier: GPL-2.0-or-later
// Start an FMOD Thread and wait for its startup semaphore. Failure cleanup uses
// saved semaphore handles; the startup wait and notification read current fields.
#include "output_threads.h"
#include "thread_services.h"

namespace {

constexpr int32_t lowest_thread_priority = -2;
constexpr int32_t highest_thread_priority = 3;
constexpr uint32_t invalid_thread_priority = 0x25;
constexpr uint32_t start_notification_selector = 3;

RT_GLOBAL(0x04c7cfa8, default_thread_name, const char);
RT_ORIGINAL(0x01118300, fmod_thread_entry, void *(audio::FmodThreadStorage *));
RT_ORIGINAL(0x0111ad10, create_thread_semaphore, uint32_t(void **semaphore));
RT_ORIGINAL(0x011c4810, copy_thread_name, void *(void *destination, const void *source));
RT_ORIGINAL(0x011c4840, copy_thread_name_counted,
            void *(void *destination, const void *source, uint32_t count));
// The caller supplies the fifth argument, but this helper does not read it.
RT_ORIGINAL(0x0111a9f0, create_fmod_native_thread,
            uint32_t(const char *name, void *entry, audio::FmodThreadStorage *argument,
                     int32_t priority, uint64_t unknown_parameter, int32_t stack_bytes,
                     uint64_t *native_handle));

}  // namespace

extern "C" uint32_t bb_audio_fmod_start_thread(audio::FmodThreadStorage *thread,
    const char *name, void *entry, void *argument, int32_t priority,
    uint64_t unknown_parameter, int32_t stack_bytes, uint8_t create_wakeup,
    uint32_t callback_delay, void *service_context)
{
    thread->entry_address = entry;
    thread->argument = argument;
    thread->callback_delay = callback_delay;
    thread->running = 0;
    thread->service_context = service_context;

    uint32_t result = create_thread_semaphore(&thread->startup_semaphore);
    if (result != 0)
        return result;

    void *const startup_semaphore = thread->startup_semaphore;
    void *wakeup_semaphore = nullptr;
    if (create_wakeup != 0) {
        result = create_thread_semaphore(&thread->wakeup_semaphore);
        if (result != 0) {
            if (startup_semaphore != nullptr)
                audio::delete_thread_semaphore(startup_semaphore);
            return result;
        }
        wakeup_semaphore = thread->wakeup_semaphore;
    }

    result = invalid_thread_priority;
    if (priority >= lowest_thread_priority && priority <= highest_thread_priority) {
        if (name != nullptr)
            copy_thread_name_counted(thread->name, name, audio::fmod_thread_name_bytes);
        else
            copy_thread_name(thread->name, default_thread_name.address());

        result = create_fmod_native_thread(thread->name, fmod_thread_entry.address(), thread,
            priority, unknown_parameter, stack_bytes, &thread->native_handle);
        if (result == 0) {
            // Thread creation can change this slot before the startup wait.
            result = audio::wait_thread_semaphore(thread->startup_semaphore);
            if (result == 0) {
                const audio::ThreadNotification notify = audio::thread_services.get()->notification;
                if (notify != nullptr)
                    notify(service_context, start_notification_selector, thread->native_handle, name);
                return 0;
            }
        }
    }

    // Cleanup errors do not replace the creation, priority or wait result.
    if (wakeup_semaphore != nullptr)
        audio::delete_thread_semaphore(wakeup_semaphore);
    if (startup_semaphore != nullptr)
        audio::delete_thread_semaphore(startup_semaphore);
    return result;
}
