/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_WRITE_WATCH_H
#define RUNTIME_WRITE_WATCH_H

/* Which pages of direct memory the CPU wrote since the renderer last asked (docs/RENDERER.md,
   "Guest memory on the GPU"). Every guest map of the direct-memory pool is watched: the guest's
   writes, and the kernel's on its behalf (a file read into the memory), are noted page by page,
   and the renderer loads a texture or target again only when its memory was written. The
   renderer's own mapping of the pool (its GPU's writes go there) is not watched.

   On Linux this is userfaultfd's asynchronous write protection (UFFD_FEATURE_WP_ASYNC, Linux 6.7)
   with PAGEMAP_SCAN, which reads the written pages and protects them again in one step, so no
   write between the two is lost. The kernel resolves the write faults itself: there is no signal
   handler, and a system call writing into a protected page succeeds. A userfaultfd limited to
   user-mode faults (UFFD_USER_MODE_ONLY) needs no privilege. Where any of that is missing, the
   watch is off and the renderer takes all memory as changed at each buffer, as before. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts watching a guest map of `length` bytes of the pool at `host`, which maps pool offset
   `pool_offset`; its pages count as written once. 0, or -1 when the watch is off (it then stays
   off: every later collect answers -1). */
int rt_write_watch_add(void *host, uint64_t length, uint64_t pool_offset);

/* Stops watching what the guest unmapped in [host, host + length) (the kernel forgets an unmapped
   range's protection by itself). */
void rt_write_watch_remove(void *host, uint64_t length);

/* Calls `written` for each run of pool bytes written through a watched map since the last collect
   (or since it was added), and protects them again. 0, or -1 when the watch is off: then anything
   may have been written. */
typedef void rt_write_watch_visit(void *context, uint64_t pool_offset, uint64_t length);
int rt_write_watch_collect(rt_write_watch_visit *written, void *context);

/* Whether the watch is on: a map was added and nothing failed. */
int rt_write_watch_active(void);

#ifdef __cplusplus
}
#endif

#endif
