// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize the embedded FMOD Thread transport fields. The original leaves its
// name, service context and padding untouched; whole-object clearing loses them.
#include "output_threads.h"
#include "thread_vtable.h"

extern "C" void bb_audio_fmod_initialize_thread(audio::FmodThreadStorage *thread)
{
    thread->vtable = audio::fmod_thread_vtable.address();
    thread->native_handle = 0;
    thread->running = 0;
    thread->callback_delay = 0;
    thread->entry_address = nullptr;
    thread->startup_semaphore = nullptr;
    thread->wakeup_semaphore = nullptr;
    thread->unknown_0x0128 = 0;
    thread->argument = nullptr;
}
