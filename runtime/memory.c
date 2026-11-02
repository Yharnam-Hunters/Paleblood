/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime/memory.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

void *rt_map_anonymous_at(void *address, size_t size, int protection)
{
    long native_page_size = sysconf(_SC_PAGESIZE);
    if (native_page_size <= 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    size_t page_size = (size_t)native_page_size;
    uintptr_t start = (uintptr_t)address;
    if (!address || !size || start % page_size || size > SIZE_MAX - (page_size - 1)) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    size_t rounded_size = ((size + page_size - 1) / page_size) * page_size;
    if (rounded_size > UINTPTR_MAX - start) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#endif
    void *mapped = mmap(address, size, protection, flags, -1, 0);
    if (mapped == MAP_FAILED || mapped == address)
        return mapped;

    /* A hint may be declined, including on kernels that ignore an unknown no-replace flag.
       Only release the newly returned allocation; the requested range remains untouched. */
    if (!munmap(mapped, size))
        errno = EADDRNOTAVAIL;
    return MAP_FAILED;
}
