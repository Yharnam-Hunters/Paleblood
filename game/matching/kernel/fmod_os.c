// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the FMOD operating-system layer's kernel wrappers:
// file, mutex and semaphore calls. A failing file call becomes FMOD's file error 0x13, a short
// read 0x16, and a failing mutex or semaphore call the generic error 0x21.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FILE_ERROR = 0x13,
    END_OF_FILE = 0x16,
    KERNEL_ERROR = 0x21,
    MUTEX_FREE_SOURCE_LINE = 0x234,
    SEMAPHORE_WAIT_ONE = 1,
    SEEK_FROM_START = 0,
};

typedef struct {
    uint8_t unknown_0x00[8];
    void *memory_pool;
} FmodGlobals;

extern FmodGlobals *fmod_globals;

int sceKernelClose(long descriptor);
long sceKernelRead(long descriptor, void *buffer, unsigned long size);
long sceKernelLseek(long descriptor, long offset, int whence);
int scePthreadMutexDestroy(void *mutex);
int scePthreadMutexUnlock(void *mutex);
int sceKernelDeleteSema(void *semaphore);
int sceKernelWaitSema(void *semaphore, int count, unsigned *timeout);
void fmod_memory_free(void *pool, void *pointer, const char *file, int line);

int kernel_file_close(long descriptor)
{
    return sceKernelClose(descriptor) ? FILE_ERROR : 0;
}

// Reads up to size bytes and stores how many arrived; fewer than asked is end of file.
int kernel_file_read_count(long descriptor, void *buffer, unsigned size, unsigned *bytes_read)
{
    long count = sceKernelRead(descriptor, buffer, size);
    if (count < 0) {
        *bytes_read = 0;
        return FILE_ERROR;
    }
    *bytes_read = count;
    return count == size ? 0 : END_OF_FILE;
}

int kernel_file_seek_absolute(long descriptor, unsigned offset)
{
    return sceKernelLseek(descriptor, offset, SEEK_FROM_START) < 0 ? FILE_ERROR : 0;
}

// Destroys the mutex, then frees its storage unless the caller owns it statically.
int kernel_mutex_destroy(void *mutex, bool static_storage)
{
    if (scePthreadMutexDestroy(mutex))
        return KERNEL_ERROR;
    if (!static_storage)
        fmod_memory_free(fmod_globals->memory_pool, mutex,
                         "N:\\FMOD\\Source\\fmodexsrc44450\\fmod4\\orbis\\src\\fmod_os_misc.cpp",
                         MUTEX_FREE_SOURCE_LINE);
    return 0;
}

// A null mutex is accepted and does nothing.
int kernel_mutex_unlock(void *mutex)
{
    if (!mutex)
        return 0;
    return scePthreadMutexUnlock(mutex) ? KERNEL_ERROR : 0;
}

int kernel_semaphore_destroy(void *semaphore)
{
    return sceKernelDeleteSema(semaphore) ? KERNEL_ERROR : 0;
}

// Waits without a timeout for one unit.
int kernel_semaphore_wait(void *semaphore)
{
    return sceKernelWaitSema(semaphore, SEMAPHORE_WAIT_ONE, NULL) ? KERNEL_ERROR : 0;
}
