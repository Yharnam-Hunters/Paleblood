/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The CPU's writes to direct memory, page by page (runtime/write_watch.h). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime/write_watch.h"

#include <pthread.h>
#include <stddef.h>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(__linux__) && defined(PAGEMAP_SCAN) && defined(UFFD_FEATURE_WP_ASYNC) && defined(UFFD_USER_MODE_ONLY)

enum { MAX_WATCHED = 4096, SCAN_REGIONS = 256, MAX_GUARDS = 1024 };

/* A watched guest map: where it is, and the pool offset it maps. `fresh` until its first collect,
   which reports it whole: nothing says what the memory held before it was watched. */
typedef struct {
    uintptr_t host;
    uint64_t length, pool_offset;
    int fresh;
    int protection; /* the host protection the guest gave it (PROT_READ, PROT_WRITE) */
} watched_map;

/* A guarded pool range (rt_write_watch_guard). */
typedef struct {
    uint64_t pool_offset, length;
} guard_range;

static pthread_mutex_t watch_lock = PTHREAD_MUTEX_INITIALIZER;
static watched_map watched[MAX_WATCHED];
static int watched_count;
static int fault_fd = -1, pagemap_fd = -1;
static int watch_off; /* something failed: every collect answers -1 */
static guard_range guards[MAX_GUARDS];
static int guard_count;
static rt_write_watch_resolver *resolver;
static uint64_t page_bytes;

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

static void apply_guards(uint64_t from, uint64_t to);

int rt_write_watch_add(void *host, uint64_t length, uint64_t pool_offset, int protection)
{
    pthread_mutex_lock(&watch_lock);
    int ok = !watch_off && watched_count < MAX_WATCHED && open_watch() == 0;
    if (ok) {
        struct uffdio_register registration = {.range = {(uintptr_t)host, length}, .mode = UFFDIO_REGISTER_MODE_WP};
        struct uffdio_writeprotect protect = {.range = {(uintptr_t)host, length}, .mode = UFFDIO_WRITEPROTECT_MODE_WP};
        ok = ioctl(fault_fd, UFFDIO_REGISTER, &registration) == 0 && ioctl(fault_fd, UFFDIO_WRITEPROTECT, &protect) == 0;
    }
    if (ok) {
        watched[watched_count++] = (watched_map){(uintptr_t)host, length, pool_offset, 1, protection};
        apply_guards(pool_offset, pool_offset + length); /* the pool may be guarded already */
    } else {
        watch_off = 1;
    }
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
        const watched_map before = {w->host, start > w->host ? start - w->host : 0, w->pool_offset, w->fresh, w->protection};
        const watched_map after = {end, end < w_end ? w_end - end : 0, w->pool_offset + (end - w->host), w->fresh, w->protection};
        /* the map's pieces outside the range stay watched */
        *w = watched[--watched_count];
        i--;
        if (before.length) watched[watched_count++] = before;
        if (after.length && watched_count < MAX_WATCHED) watched[watched_count++] = after;
        else if (after.length) watch_off = 1;
    }
    pthread_mutex_unlock(&watch_lock);
}

/* Reports the pages of [from, end) inside one map written since they were last scanned and protects
   them again, in one step (PM_SCAN_WP_MATCHING with PM_SCAN_CHECK_WPASYNC). 0, or -1. */
static int scan_map(const watched_map *w, uintptr_t from, uintptr_t end, rt_write_watch_visit *written, void *context)
{
    struct page_region regions[SCAN_REGIONS];
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

/* Scans the parts of one map inside the ranges (ascending): found by a binary search for the first
   range ending past the map's start. 0, or -1. */
static int scan_ranges(const watched_map *w, const uint64_t (*ranges)[2], size_t count, rt_write_watch_visit *written, void *context)
{
    const uint64_t map_end = w->pool_offset + w->length;
    size_t low = 0, high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (ranges[middle][0] + ranges[middle][1] <= w->pool_offset) low = middle + 1;
        else high = middle;
    }
    for (size_t r = low; r < count && ranges[r][0] < map_end; r++) {
        const uint64_t from = ranges[r][0] > w->pool_offset ? ranges[r][0] : w->pool_offset;
        const uint64_t to = ranges[r][0] + ranges[r][1] < map_end ? ranges[r][0] + ranges[r][1] : map_end;
        if (from < to && scan_map(w, w->host + (from - w->pool_offset), w->host + (to - w->pool_offset), written, context)) return -1;
    }
    return 0;
}

int rt_write_watch_collect(const uint64_t (*ranges)[2], size_t count, rt_write_watch_visit *written, void *context)
{
    pthread_mutex_lock(&watch_lock);
    int ok = !watch_off && fault_fd >= 0;
    for (int i = 0; ok && i < watched_count; i++) {
        watched_map *w = &watched[i];
        if (w->fresh) { /* reported whole; the scan only protects again what was written since it was added */
            ok = scan_map(w, w->host, w->host + w->length, ignore_written, NULL) == 0;
            written(context, w->pool_offset, w->length);
            w->fresh = 0;
            continue;
        }
        ok = scan_ranges(w, ranges, count, written, context) == 0;
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

/* ---- guards ---- */

/* Gives the pages of every watched map over pool [from, to) (widened to whole pages) PROT_NONE, or
   back their map's protection; watch_lock held. 0, or -1 when the host refused. */
static int protect_pool(uint64_t from, uint64_t to, int none)
{
    if (!page_bytes) page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    from &= ~(page_bytes - 1), to = (to + page_bytes - 1) & ~(page_bytes - 1);
    int ok = 1;
    for (int i = 0; i < watched_count; i++) {
        const watched_map *w = &watched[i];
        const uint64_t map_end = w->pool_offset + w->length;
        const uint64_t start = from > w->pool_offset ? from : w->pool_offset, end = to < map_end ? to : map_end;
        if (start >= end) continue;
        ok &= mprotect((void *)(w->host + (start - w->pool_offset)), end - start, none ? PROT_NONE : w->protection) == 0;
    }
    return ok ? 0 : -1;
}

/* Takes the pages of the guards over pool [from, to) again (after their protection changed); watch_lock held. */
static void apply_guards(uint64_t from, uint64_t to)
{
    for (int g = 0; g < guard_count; g++) {
        const uint64_t guard_end = guards[g].pool_offset + guards[g].length;
        const uint64_t start = guards[g].pool_offset > from ? guards[g].pool_offset : from, end = guard_end < to ? guard_end : to;
        if (start < end) protect_pool(start, end, 1);
    }
}

/* The guards over pool [from, to), copied into found (up to max); watch_lock held. How many. */
static int guards_over(uint64_t from, uint64_t to, guard_range *found, int max)
{
    int n = 0;
    for (int g = 0; g < guard_count && n < max; g++)
        if (guards[g].pool_offset < to && from < guards[g].pool_offset + guards[g].length) found[n++] = guards[g];
    return n;
}

/* A fault on a guarded page, handed from the faulting thread to the guard thread. */
typedef struct fault_request {
    uint64_t page; /* its pool offset */
    sem_t done;
    struct fault_request *next;
} fault_request;

static _Atomic(fault_request *) requests;
static sem_t requests_waiting;
static int guard_thread_started;
static struct sigaction previous_fault_action;

/* Resolves the guards over one pool page (the resolver lifts them), then checks they are gone. */
static void resolve_page(uint64_t page)
{
    static guard_range found[MAX_GUARDS]; /* the guard thread's own */
    pthread_mutex_lock(&watch_lock);
    const int n = guards_over(page, page + page_bytes, found, MAX_GUARDS);
    rt_write_watch_resolver *const resolve = resolver;
    pthread_mutex_unlock(&watch_lock);
    for (int i = 0; i < n; i++) resolve(found[i].pool_offset, found[i].length);
    pthread_mutex_lock(&watch_lock);
    const int left = guards_over(page, page + page_bytes, found, 1);
    pthread_mutex_unlock(&watch_lock);
    if (left) {
        fprintf(stderr, "write watch: the resolver left pool page %#llx guarded\n", (unsigned long long)page);
        abort();
    }
}

/* The guard thread: resolves each fault handed to it, on its own stack (the resolver may run the
   renderer), then lets the faulting thread go on. */
static void *guard_thread(void *unused)
{
    (void)unused;
    for (;;) {
        while (sem_wait(&requests_waiting) && errno == EINTR) {
        }
        fault_request *r = atomic_exchange(&requests, NULL);
        while (r) {
            fault_request *next = r->next;
            resolve_page(r->page);
            sem_post(&r->done);
            r = next;
        }
    }
    return NULL;
}

/* The pool page under a guest address in a watched map, when a guard lies over it; watch_lock held. */
static int guarded_page(uintptr_t at, uint64_t *page)
{
    for (int i = 0; i < watched_count; i++) {
        const watched_map *w = &watched[i];
        if (at < w->host || at - w->host >= w->length) continue;
        const uint64_t p = (w->pool_offset + (at - w->host)) & ~(page_bytes - 1);
        guard_range found;
        if (guards_over(p, p + page_bytes, &found, 1)) return *page = p, 1;
    }
    return 0;
}

static void on_fault(int sig, siginfo_t *info, void *context)
{
    uint64_t page = 0;
    int ours = 0;
    if (info->si_code == SEGV_ACCERR) {
        /* no code holding the lock touches guest memory, so this thread does not hold it */
        pthread_mutex_lock(&watch_lock);
        ours = guarded_page((uintptr_t)info->si_addr, &page);
        pthread_mutex_unlock(&watch_lock);
    }
    if (ours) {
        fault_request r = {.page = page};
        sem_init(&r.done, 0, 0);
        r.next = atomic_load(&requests);
        while (!atomic_compare_exchange_weak(&requests, &r.next, &r)) {
        }
        sem_post(&requests_waiting);
        while (sem_wait(&r.done) && errno == EINTR) {
        }
        sem_destroy(&r.done);
        return; /* the access runs again on the resolved page */
    }
    if (previous_fault_action.sa_flags & SA_SIGINFO) previous_fault_action.sa_sigaction(sig, info, context);
    else if (previous_fault_action.sa_handler == SIG_DFL || previous_fault_action.sa_handler == SIG_IGN) signal(sig, SIG_DFL); /* faults again */
    else previous_fault_action.sa_handler(sig);
}

/* Starts the guard thread and the fault handler, once; watch_lock held. 0, or -1. */
static int start_guarding(void)
{
    if (guard_thread_started) return 0;
    if (sem_init(&requests_waiting, 0, 0)) return -1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, guard_thread, NULL)) return -1;
    pthread_detach(thread);
    struct sigaction action = {0};
    action.sa_sigaction = on_fault;
    action.sa_flags = SA_SIGINFO | SA_NODEFER; /* on the faulting thread's own stack: it only waits there */
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, &previous_fault_action)) return -1;
    guard_thread_started = 1;
    return 0;
}

void rt_write_watch_set_resolver(rt_write_watch_resolver *resolve)
{
    pthread_mutex_lock(&watch_lock);
    resolver = resolve;
    pthread_mutex_unlock(&watch_lock);
}

int rt_write_watch_guard(uint64_t pool_offset, uint64_t length)
{
    pthread_mutex_lock(&watch_lock);
    if (!page_bytes) page_bytes = (uint64_t)sysconf(_SC_PAGESIZE);
    int ok = !watch_off && fault_fd >= 0 && resolver && guard_count < MAX_GUARDS && length && start_guarding() == 0;
    if (ok) {
        guards[guard_count++] = (guard_range){pool_offset, length};
        if (protect_pool(pool_offset, pool_offset + length, 1)) { /* undone: the caller keeps the memory current */
            guard_count--;
            protect_pool(pool_offset, pool_offset + length, 0);
            apply_guards(pool_offset & ~(page_bytes - 1), (pool_offset + length + page_bytes - 1) & ~(page_bytes - 1));
            ok = 0;
        }
    }
    pthread_mutex_unlock(&watch_lock);
    return ok ? 0 : -1;
}

void rt_write_watch_unguard(uint64_t pool_offset, uint64_t length)
{
    pthread_mutex_lock(&watch_lock);
    const uint64_t end = pool_offset + length;
    for (int g = 0; g < guard_count; g++) {
        const guard_range r = guards[g];
        if (r.pool_offset < pool_offset || r.pool_offset + r.length > end) continue;
        guards[g--] = guards[--guard_count];
        protect_pool(r.pool_offset, r.pool_offset + r.length, 0);
        /* pages shared with another guard stay with it */
        apply_guards(r.pool_offset & ~(page_bytes - 1), (r.pool_offset + r.length + page_bytes - 1) & ~(page_bytes - 1));
    }
    pthread_mutex_unlock(&watch_lock);
}

void rt_write_watch_touch(const void *host, uint64_t length)
{
    const uintptr_t start = (uintptr_t)host, end = start + length;
    guard_range found[64];
    int n = 0;
    pthread_mutex_lock(&watch_lock);
    for (int i = 0; i < watched_count && guard_count; i++) {
        const watched_map *w = &watched[i];
        const uintptr_t from = start > w->host ? start : w->host, to = end < w->host + w->length ? end : w->host + w->length;
        if (from < to) n += guards_over(w->pool_offset + (from - w->host), w->pool_offset + (to - w->host), found + n, 64 - n);
    }
    rt_write_watch_resolver *const resolve = resolver;
    pthread_mutex_unlock(&watch_lock);
    for (int i = 0; i < n; i++) resolve(found[i].pool_offset, found[i].length);
}

void rt_write_watch_protect(void *host, uint64_t length, int protection)
{
    const uintptr_t start = (uintptr_t)host, end = start + length;
    pthread_mutex_lock(&watch_lock);
    for (int i = 0; i < watched_count; i++) {
        watched_map *w = &watched[i];
        const uintptr_t w_end = w->host + w->length;
        if (end <= w->host || w_end <= start) continue;
        if (start <= w->host && w_end <= end) { /* all of it */
            w->protection = protection;
            continue;
        }
        if (watched_count + 2 > MAX_WATCHED) {
            watch_off = 1;
            break;
        }
        /* split: the part inside takes the protection */
        const uintptr_t from = start > w->host ? start : w->host, to = end < w_end ? end : w_end;
        const watched_map before = {w->host, from - w->host, w->pool_offset, w->fresh, w->protection};
        const watched_map after = {to, w_end - to, w->pool_offset + (to - w->host), w->fresh, w->protection};
        *w = (watched_map){from, to - from, w->pool_offset + (from - w->host), w->fresh, protection};
        if (before.length) watched[watched_count++] = before;
        if (after.length) watched[watched_count++] = after;
    }
    for (int i = 0; i < watched_count; i++) {
        const watched_map *w = &watched[i];
        const uintptr_t from = start > w->host ? start : w->host, to = end < w->host + w->length ? end : w->host + w->length;
        if (from < to) apply_guards(w->pool_offset + (from - w->host), w->pool_offset + (to - w->host));
    }
    pthread_mutex_unlock(&watch_lock);
}

#else /* no asynchronous write protection here: the watch is off */

int rt_write_watch_add(void *host, uint64_t length, uint64_t pool_offset, int protection)
{
    (void)host, (void)length, (void)pool_offset, (void)protection;
    return -1;
}

void rt_write_watch_protect(void *host, uint64_t length, int protection) { (void)host, (void)length, (void)protection; }
void rt_write_watch_set_resolver(rt_write_watch_resolver *resolve) { (void)resolve; }

int rt_write_watch_guard(uint64_t pool_offset, uint64_t length)
{
    (void)pool_offset, (void)length;
    return -1;
}

void rt_write_watch_unguard(uint64_t pool_offset, uint64_t length) { (void)pool_offset, (void)length; }
void rt_write_watch_touch(const void *host, uint64_t length) { (void)host, (void)length; }

void rt_write_watch_remove(void *host, uint64_t length) { (void)host, (void)length; }

int rt_write_watch_collect(const uint64_t (*ranges)[2], size_t count, rt_write_watch_visit *written, void *context)
{
    (void)ranges, (void)count, (void)written, (void)context;
    return -1;
}

int rt_write_watch_active(void) { return 0; }

#endif
