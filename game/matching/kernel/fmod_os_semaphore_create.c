// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the FMOD operating-system layer's semaphore creation.
// The original checks the stack canary (the handle's address is taken): match with ps4-o2-sspall.

#include <stddef.h>

enum {
    KERNEL_ERROR = 0x21,
    SEMAPHORE_ATTRIBUTE_FIFO = 1,
    SEMAPHORE_INITIAL_COUNT = 0,
    SEMAPHORE_MAX_COUNT = 0xffff,
};

int sceKernelCreateSema(void **semaphore, const char *name, unsigned attribute, int initial,
                        int max, const void *options);

// Creates an empty FIFO semaphore and stores its handle on success.
int kernel_semaphore_create(void **semaphore)
{
    void *handle;
    if (sceKernelCreateSema(&handle, "FMOD Semaphore", SEMAPHORE_ATTRIBUTE_FIFO,
                            SEMAPHORE_INITIAL_COUNT, SEMAPHORE_MAX_COUNT, NULL))
        return KERNEL_ERROR;
    *semaphore = handle;
    return 0;
}
