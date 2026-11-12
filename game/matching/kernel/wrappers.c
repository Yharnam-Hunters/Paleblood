// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the engine's thin wrappers over kernel calls. A failing
// call becomes the engine's generic error 0x21.

#define ENGINE_KERNEL_ERROR 0x21
#define SIGNAL_ONE 1

int sceKernelSignalSema(void *semaphore, int count);
int scePthreadMutexLock(void *mutex);
void *scePthreadSelf(void);

int kernel_semaphore_signal(void *semaphore)
{
    return sceKernelSignalSema(semaphore, SIGNAL_ONE) ? ENGINE_KERNEL_ERROR : 0;
}

// A null mutex is accepted and does nothing.
int kernel_mutex_lock(void *mutex)
{
    if (!mutex)
        return 0;
    return scePthreadMutexLock(mutex) ? ENGINE_KERNEL_ERROR : 0;
}

int kernel_thread_self(void **thread)
{
    *thread = scePthreadSelf();
    return 0;
}
