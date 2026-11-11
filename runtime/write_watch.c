/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The CPU's writes to direct memory, page by page (runtime/write_watch.h). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime/write_watch.h"

#include <pthread.h>
#include <stddef.h>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(__linux__) && defined(PAGEMAP_SCAN) && defined(UFFD_FEATURE_WP_ASYNC) && defined(UFFD_USER_MODE_ONLY)

enum { MAX_WATCHED = 4096, SCAN_REGIONS = 256 };

/* A watched guest map: where it is, and the pool offset it maps. `fresh` until its first collect,
   which reports it whole: nothing says what the memory held before it was watched. */
typedef struct {
    uintptr_t host;
    uint64_t length, pool_offset;
    int fresh;
} watched_map;

static pthread_mutex_t watch_lock = PTHREAD_MUTEX_INITIALIZER;
static watched_map watched[MAX_WATCHED];
static int watched_count;
static int fault_fd = -1, pagemap_fd = -1;
static int watch_off; /* something failed: every collect answers -1 */

/* Opens the userfaultfd (user-mode faults only, resolved by the kernel: WP_ASYNC; shared memory and
   pages not yet touched protected too) and the pagemap; watch_lock held. 0, or -1. */
static int open_watch(void)
{
    if (fault_fd >= 0) return 0;
    const int fd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    if (fd < 0) return -1;
    struct uffdio_api api = {.api = UFFD_API,
                             .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_HUGETLBFS_SHMEM | UFFD_FEATURE_WP_UNPOPULATED};
    const int pages = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (ioctl(fd, UFFDIO_API, &api) || pages < 0) {
        close(fd);
        if (pages >= 0) close(pages);
        return -1;
    }
    fault_fd = fd, pagemap_fd = pages;
    return 0;
}

int rt_write_watch_add(void *host, uint64_t length, uint64_t pool_offset)
{
    pthread_mutex_lock(&watch_lock);
    int ok = !watch_off && watched_count < MAX_WATCHED && open_watch() == 0;
    if (ok) {
        struct uffdio_register registration = {.range = {(uintptr_t)host, length}, .mode = UFFDIO_REGISTER_MODE_WP};
        struct uffdio_writeprotect protect = {.range = {(uintptr_t)host, length}, .mode = UFFDIO_WRITEPROTECT_MODE_WP};
        ok = ioctl(fault_fd, UFFDIO_REGISTER, &registration) == 0 && ioctl(fault_fd, UFFDIO_WRITEPROTECT, &protect) == 0;
    }
    if (ok) watched[watched_count++] = (watched_map){(uintptr_t)host, length, pool_offset, 1};
    else watch_off = 1;
    pthread_mutex_unlock(&watch_lock);
    return ok ? 0 : -1;
}

void rt_write_watch_remove(void *host, uint64_t length)
{
    const uintptr_t start = (uintptr_t)host, end = start + length;
    pthread_mutex_lock(&watch_lock);
    for (int i = 0; i < watched_count; i++) {
        watched_map *w = &watched[i];
        const uintptr_t w_end = w->host + w->length;
        if (end <= w->host || w_end <= start) continue;
        const watched_map before = {w->host, start > w->host ? start - w->host : 0, w->pool_offset, w->fresh};
        const watched_map after = {end, end < w_end ? w_end - end : 0, w->pool_offset + (end - w->host), w->fresh};
        /* the map's pieces outside the range stay watched */
        *w = watched[--watched_count];
        i--;
        if (before.length) watched[watched_count++] = before;
        if (after.length && watched_count < MAX_WATCHED) watched[watched_count++] = after;
        else if (after.length) watch_off = 1;
    }
    pthread_mutex_unlock(&watch_lock);
}

/* Reports the pages of one map written since its last scan and protects them again, in one step
   (PM_SCAN_WP_MATCHING with PM_SCAN_CHECK_WPASYNC). 0, or -1. */
static int scan_map(const watched_map *w, rt_write_watch_visit *written, void *context)
{
    struct page_region regions[SCAN_REGIONS];
    uintptr_t from = w->host;
    const uintptr_t end = w->host + w->length;
    while (from < end) {
        struct pm_scan_arg scan = {.size = sizeof scan, .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC, .start = from, .end = end,
                                   .vec = (uintptr_t)regions, .vec_len = SCAN_REGIONS, .category_mask = PAGE_IS_WRITTEN,
                                   .return_mask = PAGE_IS_WRITTEN};
        const long found = ioctl(pagemap_fd, PAGEMAP_SCAN, &scan);
        if (found < 0) return -1;
        for (long i = 0; i < found; i++) written(context, w->pool_offset + (regions[i].start - w->host), regions[i].end - regions[i].start);
        if (found < SCAN_REGIONS && scan.walk_end >= end) break;
        from = scan.walk_end; /* the region list filled up: on from where the walk stopped */
    }
    return 0;
}

static void ignore_written(void *context, uint64_t pool_offset, uint64_t length) { (void)context, (void)pool_offset, (void)length; }

int rt_write_watch_collect(rt_write_watch_visit *written, void *context)
{
    pthread_mutex_lock(&watch_lock);
    int ok = !watch_off && fault_fd >= 0;
    for (int i = 0; ok && i < watched_count; i++) {
        watched_map *w = &watched[i];
        if (w->fresh) { /* reported whole; the scan only protects again what was written since it was added */
            ok = scan_map(w, ignore_written, NULL) == 0;
            written(context, w->pool_offset, w->length);
            w->fresh = 0;
            continue;
        }
        ok = scan_map(w, written, context) == 0;
    }
    if (!ok) watch_off = 1;
    pthread_mutex_unlock(&watch_lock);
    return ok ? 0 : -1;
}

int rt_write_watch_active(void)
{
    pthread_mutex_lock(&watch_lock);
    const int active = !watch_off && fault_fd >= 0;
    pthread_mutex_unlock(&watch_lock);
    return active;
}

#else /* no asynchronous write protection here: the watch is off */

int rt_write_watch_add(void *host, uint64_t length, uint64_t pool_offset)
{
    (void)host, (void)length, (void)pool_offset;
    return -1;
}

void rt_write_watch_remove(void *host, uint64_t length) { (void)host, (void)length; }

int rt_write_watch_collect(rt_write_watch_visit *written, void *context)
{
    (void)written, (void)context;
    return -1;
}

int rt_write_watch_active(void) { return 0; }

#endif
