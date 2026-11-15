// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD output shutdown calls this helper for each Thread. Semaphore errors leave
// later teardown stages untouched; notification can replace the allocation/context.
#include "output_threads.h"
#include "thread_services.h"

static_assert(offsetof(audio::FmodThreadStorage, unknown_0x0128) == 0x128);

extern "C" uint32_t bb_audio_fmod_stop_thread(audio::FmodThreadStorage *thread)
{
    if (thread->running == 0)
        return 0;
    thread->running = 0;

    if (thread->wakeup_semaphore != nullptr) {
        const uint32_t result = audio::signal_thread_semaphore(thread->wakeup_semaphore);
        if (result != 0)
            return result;
    }

    const uint32_t wait_result = audio::wait_thread_semaphore(thread->startup_semaphore);
    if (wait_result != 0)
        return wait_result;

    // Calls can change the handles: re-read them and clear only successful deletes.
    if (thread->wakeup_semaphore != nullptr) {
        const uint32_t result = audio::delete_thread_semaphore(thread->wakeup_semaphore);
        if (result != 0)
            return result;
        thread->wakeup_semaphore = nullptr;
    }
    const uint32_t delete_result = audio::delete_thread_semaphore(thread->startup_semaphore);
    if (delete_result != 0)
        return delete_result;
    thread->startup_semaphore = nullptr;

    // The intervening original handle helper is a pure zero-return stub, not a join.
    const audio::ThreadNotification notify = audio::thread_services.get()->notification;
    if (notify != nullptr)
        notify(thread->service_context, audio::stop_notification_selector,
               thread->native_handle, thread->name);
    thread->native_handle = 0;

    // Notification can replace the block and global context. Cleanup can write back
    // to the block slot; the original overwrites that slot after the release returns.
    void *block = reinterpret_cast<void *>(static_cast<uintptr_t>(thread->unknown_0x0128));
    if (block != nullptr) {
        audio::MemoryPool *pool = audio::thread_services.get()->memory_pool;
        audio::release_thread_storage(pool, block, audio::thread_source_file.get().data(),
                                      audio::original_stop_source_line);
        thread->unknown_0x0128 = 0;
    }
    return 0;
}
