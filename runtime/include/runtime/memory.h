/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_MEMORY_H
#define RUNTIME_MEMORY_H

#include <stddef.h>
#include <sys/types.h>

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

/* The same non-replacing placement for shared file or private anonymous mappings.
   flags must select MAP_SHARED or MAP_PRIVATE and may add MAP_ANONYMOUS/MAP_NORESERVE;
   MAP_FIXED and unknown flags are rejected. fd/offset have mmap's usual meaning.
   A declined hint is cleaned up and fails with EADDRNOTAVAIL; that does not prove
   occupancy. The caller must serialize any later replacement of its owned ranges. */
void *rt_map_at(void *address, size_t size, int protection, int flags, int fd, off_t offset);

#ifdef __cplusplus
}
#endif

#endif
