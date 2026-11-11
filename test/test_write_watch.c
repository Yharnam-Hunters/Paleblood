/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The CPU write watch on direct memory (runtime/write_watch.h): a guest map of a memory file is
   watched, a second map of the same file (the renderer's) is not.
   - the first collect reports the whole map (nothing says what it held before), the next nothing;
   - a write through the watched map and a file read into it (the kernel writing) are reported, at
     their pool offsets; a write through the other map is not; a page is reported once per write;
   - a collect reports only inside the ranges it is given; a write outside them is reported by the
     first collect that takes it in;
   - a range taken out of the watch (unmapped) is no longer reported, the rest still is;
   - a write made after each collect is reported by the next, 1000 times over with the writer on
     another thread (the scan protects again what it reports, in one step).
   Skipped (77) where the host has no asynchronous write protection. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime/write_watch.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { PAGES = 64, POOL_OFFSET = 0x40000, SKIPPED = 77, ROUNDS = 1000 };
static uint64_t page_size;

/* What a collect reported: a mark per page of the map, and how many runs. */
typedef struct {
    int pages[PAGES];
    int runs;
} reported;

static void note(void *context, uint64_t pool_offset, uint64_t length)
{
    reported *r = context;
    r->runs++;
    for (uint64_t at = pool_offset; at < pool_offset + length; at += page_size) {
        const uint64_t page = (at - POOL_OFFSET) / page_size;
        if (at >= POOL_OFFSET && page < PAGES) r->pages[page]++;
    }
}

static const uint64_t everything[1][2] = {{0, UINT64_MAX / 2}};

static reported collect(void)
{
    reported r = {{0}, 0};
    CHECK(rt_write_watch_collect(everything, 1, note, &r) == 0);
    return r;
}

static int count_pages(const reported *r)
{
    int n = 0;
    for (int i = 0; i < PAGES; i++) n += r->pages[i] != 0;
    return n;
}

static unsigned char *guest;
static atomic_int round_written, round_seen;

/* Writes a page, then waits for the main thread to have seen it, ROUNDS times. */
static void *writer(void *unused)
{
    (void)unused;
    for (int round = 1; round <= ROUNDS; round++) {
        guest[(uint64_t)(round % PAGES) * page_size] = (unsigned char)round;
        atomic_store(&round_written, round);
        while (atomic_load(&round_seen) < round) sched_yield();
    }
    return NULL;
}

int main(void)
{
    page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t length = PAGES * page_size;
    const int fd = memfd_create("write-watch-test", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)(POOL_OFFSET + length))) return 1;
    guest = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, POOL_OFFSET);
    unsigned char *other = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, POOL_OFFSET);
    if (guest == MAP_FAILED || other == MAP_FAILED) return 1;
    guest[0] = 1; /* touched before the watch */
    if (rt_write_watch_add(guest, length, POOL_OFFSET)) {
        fprintf(stderr, "no asynchronous write protection here: skipped\n");
        return SKIPPED;
    }
    CHECK(rt_write_watch_active());

    reported r = collect();
    CHECK(count_pages(&r) == PAGES);
    r = collect();
    CHECK(count_pages(&r) == 0);

    guest[3 * page_size + 17] = 2;
    other[5 * page_size] = 3;
    int pipe_fds[2];
    CHECK(pipe(pipe_fds) == 0);
    CHECK(write(pipe_fds[1], "hunter", 6) == 6);
    CHECK(read(pipe_fds[0], guest + 7 * page_size + 100, 6) == 6); /* the kernel writes the page */
    CHECK(memcmp(other + 7 * page_size + 100, "hunter", 6) == 0);
    r = collect();
    CHECK(count_pages(&r) == 2 && r.pages[3] == 1 && r.pages[7] == 1);
    r = collect();
    CHECK(count_pages(&r) == 0);

    /* asked about page 10 only: page 12's write waits for a collect that takes it in */
    guest[10 * page_size] = 7, guest[12 * page_size] = 8;
    const uint64_t page_ten[1][2] = {{POOL_OFFSET + 10 * page_size, page_size}};
    r = (reported){{0}, 0};
    CHECK(rt_write_watch_collect(page_ten, 1, note, &r) == 0);
    CHECK(count_pages(&r) == 1 && r.pages[10] == 1);
    r = collect();
    CHECK(count_pages(&r) == 1 && r.pages[12] == 1);

    rt_write_watch_remove(guest + 2 * page_size, 2 * page_size); /* pages 2 and 3 */
    guest[3 * page_size] = 4, guest[8 * page_size] = 5, guest[1 * page_size] = 6;
    r = collect();
    CHECK(count_pages(&r) == 2 && r.pages[1] == 1 && r.pages[8] == 1);

    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, writer, NULL) == 0);
    int missed = 0;
    for (int round = 1; round <= ROUNDS; round++) {
        while (atomic_load(&round_written) < round) sched_yield();
        r = collect();
        const int page = round % PAGES;
        missed += page != 2 && page != 3 && r.pages[page] != 1;
        atomic_store(&round_seen, round);
    }
    pthread_join(thread, NULL);
    CHECK(missed == 0);

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
