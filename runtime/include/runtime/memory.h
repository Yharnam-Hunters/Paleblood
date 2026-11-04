/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_MEMORY_H
#define RUNTIME_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map anonymous private memory at exactly address without replacing any existing mapping.
   Address must be non-null and page-aligned; size must be nonzero and its rounded extent must
   not wrap. Invalid requests fail with MAP_FAILED/EINVAL. Protection uses mmap's PROT_* flags.

   Linux uses MAP_FIXED_NOREPLACE when available at build time. Other POSIX hosts use an
   ordinary hint, releasing any result at another address before failing with EADDRNOTAVAIL.
   A declined hint does not prove the requested address is occupied. A cleanup failure instead
   preserves munmap's errno. The caller owns a successful mapping and must munmap it. */
void *rt_map_anonymous_at(void *address, size_t size, int protection);

#ifdef __cplusplus
}
#endif

#endif
