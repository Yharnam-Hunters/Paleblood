// SPDX-License-Identifier: GPL-2.0-or-later
// Wake an FMOD Thread through its optional semaphore. The signal helper supplies
// the fixed count and translates the kernel's DWORD status.
#include "output_threads.h"
#include "thread_services.h"

extern "C" uint32_t bb_audio_fmod_wake_thread(audio::FmodThreadStorage *thread)
{
    void *const wakeup = thread->wakeup_semaphore;
    if (wakeup == nullptr)
        return 0;
    return audio::signal_thread_semaphore(wakeup);
}
