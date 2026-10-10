/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "runtime/capture.h"

enum { FIRST = 50, EVERY = 1000, MAX_CASES = 200, MAX_FUNCTIONS = 64 };

static struct { char name[128]; atomic_uint calls; atomic_int recorded; } slots[MAX_FUNCTIONS];
static int slot_count;
static atomic_flag slots_lock = ATOMIC_FLAG_INIT;
static atomic_flag run_lock = ATOMIC_FLAG_INIT;
static atomic_int run_state;
static char id[64];
static atomic_int warned;

static int component(const char *s)
{
    if (!s || !*s || !strcmp(s, ".") || !strcmp(s, ".."))
        return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_' || *s == '-' || *s == '.'))
            return 0;
    return 1;
}

static void warn_once(const char *message, const char *path)
{
    if (!atomic_exchange(&warned, 1))
        fprintf(stderr, "capture: %s%s%s; nothing is recorded\n", message, path ? ": " : "", path ? path : "");
}

static int slot_for(const char *function)
{
    if (strlen(function) >= sizeof slots[0].name)
        return -1;
    while (atomic_flag_test_and_set(&slots_lock)) {}
    int found = -1;
    for (int i = 0; i < slot_count; i++)
        if (!strcmp(slots[i].name, function)) {
            found = i;
            break;
        }
    if (found < 0 && slot_count < MAX_FUNCTIONS) {
        found = slot_count++;
        snprintf(slots[found].name, sizeof slots[found].name, "%s", function);
    }
    atomic_flag_clear(&slots_lock);
    return found;
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
    if (!getenv("BB_CAPTURE_DIR") || !component(function) || !rt_capture_run_id())
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
const char *rt_capture_run_id(void)
{
    if (!getenv("BB_CAPTURE_DIR"))
        return NULL;
    if (!atomic_load(&run_state)) {
        while (atomic_flag_test_and_set(&run_lock)) {}
        if (!atomic_load(&run_state)) {
        const char *env = getenv("BB_CAPTURE_RUN");
        if (env && *env) {
            if (!component(env) || snprintf(id, sizeof id, "%s", env) >= (int)sizeof id)
                id[0] = 0;
        } else {
            time_t t = time(NULL);
            struct tm tm;
            if (localtime_r(&t, &tm))
                snprintf(id, sizeof id, "%04d%02d%02d-%02d%02d%02d-%d", tm.tm_year + 1900, tm.tm_mon + 1,
                         tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
        }
        atomic_store(&run_state, id[0] ? 2 : -1);
        }
        atomic_flag_clear(&run_lock);
    }
    return atomic_load(&run_state) == 2 ? id : NULL;
}

/* mkdir -p: the capture directory may not exist yet, nor its parents. */
static int make_dirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            struct stat st;
            if (mkdir(path, 0700) && (errno != EEXIST || stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {
                *p = '/';
                return -1;
            }
            *p = '/';
        }
    }
    struct stat st;
    if (mkdir(path, 0700) && (errno != EEXIST || stat(path, &st) != 0 || !S_ISDIR(st.st_mode)))
        return -1;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

void rt_capture_write(const char *function, int case_number, const char *json)
{
    const char *dir = getenv("BB_CAPTURE_DIR");
    const char *run = rt_capture_run_id();
    if (!dir || !run || case_number < 0 || !component(function) || !json)
        return;
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/%s", dir, function);
    if (n < 0 || n >= (int)sizeof path || make_dirs(path)) {
        warn_once("cannot create capture directory", path);
        return;
    }
    n = snprintf(path, sizeof path, "%s/%s/%s_%04d.json", dir, function, run, case_number);
    if (n < 0 || n >= (int)sizeof path) {
        warn_once("capture path is too long", NULL);
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        warn_once(errno == EEXIST ? "refusing to overwrite existing case" : "cannot create case", path);
        return;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(path);
        warn_once("cannot open case stream", path);
        return;
    }
    int failed = fputs(json, f) == EOF || fputc('\n', f) == EOF || fflush(f) == EOF;
    if (fclose(f) == EOF)
        failed = 1;
    if (failed) {
        unlink(path);
        warn_once("cannot finish case", path);
    }
}
