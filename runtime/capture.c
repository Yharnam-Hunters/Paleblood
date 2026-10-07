/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "runtime/capture.h"

enum { FIRST = 50, EVERY = 1000, MAX_CASES = 200, MAX_FUNCTIONS = 64 };

static struct { const char *name; atomic_uint calls; atomic_int recorded; } slots[MAX_FUNCTIONS];
static atomic_int slot_count;

static int slot_for(const char *function)
{
    int n = atomic_load(&slot_count);
    for (int i = 0; i < n; i++)
        if (slots[i].name == function || !strcmp(slots[i].name, function))
            return i;
    int i = atomic_fetch_add(&slot_count, 1);
    if (i >= MAX_FUNCTIONS)
        return -1;
    slots[i].name = function;
    return i;
}

/* BB_CAPTURE_ONLY: comma-separated function names; when set, only those are recorded (a run
 * aimed at new functions should not stall on recording ones already verified). */
static int wanted(const char *function)
{
    const char *only = getenv("BB_CAPTURE_ONLY");
    if (!only || !*only)
        return 1;
    size_t len = strlen(function);
    for (const char *p = only; *p;) {
        size_t n = strcspn(p, ",");
        if (n == len && !strncmp(p, function, n))
            return 1;
        p += n + (p[n] == ',');
    }
    return 0;
}

int rt_capture_begin(const char *function)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("BB_CAPTURE_DIR") != NULL;
    if (!enabled)
        return -1;
    int s = slot_for(function);
    if (s < 0)
        return -1;
    if (!wanted(function))
        return -1;
    unsigned call = atomic_fetch_add(&slots[s].calls, 1);
    if (call >= FIRST && call % EVERY)
        return -1;
    int number = atomic_fetch_add(&slots[s].recorded, 1);
    return number < MAX_CASES ? number : -1;
}

/* Identifies one game run in file names, so runs add to the library instead of overwriting:
   BB_CAPTURE_RUN, or the start time and process id. */
static const char *run_id(void)
{
    static char id[64];
    if (!id[0]) {
        const char *env = getenv("BB_CAPTURE_RUN");
        if (env && *env) {
            snprintf(id, sizeof id, "%s", env);
        } else {
            time_t t = time(NULL);
            struct tm tm;
            localtime_r(&t, &tm);
            snprintf(id, sizeof id, "%04d%02d%02d-%02d%02d%02d-%d", tm.tm_year + 1900, tm.tm_mon + 1,
                     tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
        }
    }
    return id;
}

/* mkdir -p: the capture directory may not exist yet, nor its parents. */
static void make_dirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(path, 0755);
            *p = '/';
        }
    }
    mkdir(path, 0755);
}

void rt_capture_write(const char *function, int case_number, const char *json)
{
    const char *dir = getenv("BB_CAPTURE_DIR");
    if (!dir || case_number < 0)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, function);
    make_dirs(path);
    snprintf(path, sizeof path, "%s/%s/%s_%04d.json", dir, function, run_id(), case_number);
    FILE *f = fopen(path, "w");
    if (!f) {
        static atomic_int warned;
        if (!atomic_exchange(&warned, 1))
            fprintf(stderr, "capture: cannot write %s; nothing is recorded\n", path);
        return;
    }
    fputs(json, f);
    fputc('\n', f);
    fclose(f);
}
