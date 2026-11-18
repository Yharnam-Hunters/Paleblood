// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD Thread's pthread entry, passed by the startup helper. Calls can change
// the running byte and dispatch fields; re-read them at the original loop gates.
#include "output_threads.h"
#include "thread_services.h"
#include "thread_vtable.h"

#include <bit>

namespace {

using ThreadRoutine = void (*)(void *argument);
using ThreadMethod = void (*)(audio::FmodThreadStorage *thread);

RT_ORIGINAL(0x0111a880, frame_timing_sleep_ms, int(uint32_t milliseconds));

}  // namespace

extern "C" void *bb_audio_fmod_thread_entry(audio::FmodThreadStorage *thread)
{
    thread->running = 1;
    audio::signal_thread_semaphore(thread->startup_semaphore);

    while (thread->running != 0) {
        void *const wakeup = thread->wakeup_semaphore;
        if (wakeup != nullptr)
            audio::wait_thread_semaphore(wakeup);
        if (thread->running == 0)
            continue;

        const auto routine = std::bit_cast<ThreadRoutine>(thread->entry_address);
        if (routine != nullptr) {
            routine(thread->argument);
        } else {
            // The canonical carrier leaves the original virtual prototypes open.
            // This call passes the Thread pointer to slot zero and ignores its result.
            const auto *vtable = static_cast<const audio::FmodThreadVtable *>(thread->vtable);
            std::bit_cast<ThreadMethod>(vtable->unknown_slots[0])(thread);
        }

        const uint32_t delay = thread->callback_delay;
        if (delay != 0)
            frame_timing_sleep_ms(delay);
    }

    audio::signal_thread_semaphore(thread->startup_semaphore);
    return nullptr;
}
