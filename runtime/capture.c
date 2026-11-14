/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <dirent.h>
#include <stdatomic.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "runtime/capture.h"

enum { FIRST = 50, EVERY = 1000, MAX_CASES = 200, MAX_FUNCTIONS = 1024 };

/* Per function: calls seen, case numbers handed out, distinct cases kept in this run, and the
   digests of every case it has (the library's, read once, and this run's), for dropping repeats. */
typedef struct {
    char name[128];
    atomic_uint calls;
    atomic_int numbered;
    atomic_int kept;
    atomic_flag lock;
    int loaded;
    uint64_t *digests;
    size_t digest_count, digest_capacity;
} capture_slot;

static capture_slot slots[MAX_FUNCTIONS];
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

/* Whether the comma-separated list in the environment variable names this function. */
static int listed(const char *variable, const char *function)
{
    const char *list = getenv(variable);
    if (!list)
        return 0;
    size_t len = strlen(function);
    for (const char *p = list; *p;) {
        size_t n = strcspn(p, ",");
        if (n == len && !strncmp(p, function, n))
            return 1;
        p += n + (p[n] == ',');
    }
    return 0;
}

/* BB_CAPTURE_ONLY: comma-separated function names; when set, only those are recorded (a run
 * aimed at new functions should not stall on recording ones already verified). */
static int wanted(const char *function)
{
    const char *only = getenv("BB_CAPTURE_ONLY");
    return !only || !*only || listed("BB_CAPTURE_ONLY", function);
}

int rt_capture_native(const char *function)
{
    return function && listed("BB_CAPTURE_NATIVE", function);
}

int rt_capture_wanted(const char *function)
{
    return function && wanted(function);
}

/* A positive whole number from the environment, or `fallback`. */
static unsigned setting(const char *name, unsigned fallback)
{
    const char *env = getenv(name);
    char *end;
    const long n = env && *env ? strtol(env, &end, 10) : 0;
    return env && *env && !*end && n > 0 && n <= 1000000 ? (unsigned)n : fallback;
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
    /* BB_CAPTURE_MAX distinct cases; BB_CAPTURE_FIRST calls, then every BB_CAPTURE_EVERY-th. */
    if (atomic_load(&slots[s].kept) >= (int)setting("BB_CAPTURE_MAX", MAX_CASES))
        return -1;
    unsigned call = atomic_fetch_add(&slots[s].calls, 1);
    if (call >= setting("BB_CAPTURE_FIRST", FIRST) && call % setting("BB_CAPTURE_EVERY", EVERY))
        return -1;
    return atomic_fetch_add(&slots[s].numbered, 1);
}

/* FNV-1a over a case, leaving out the values of "id" and "run_id": two calls that read the same
   inputs give the same digest, whichever run or case number recorded them. */
static uint64_t case_digest(const char *json)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char *p = json; *p; p++) {
        if (!strncmp(p, "\"id\": \"", 7) || !strncmp(p, "\"run_id\": \"", 11)) {
            p = strchr(p, ':') + 3;                     /* the value's first character */
            while (*p && *p != '"') p++;
            if (!*p) break;
        }
        h = (h ^ (unsigned char)*p) * 0x100000001b3ull;
    }
    return h;
}

static int add_digest(capture_slot *s, uint64_t digest)
{
    for (size_t i = 0; i < s->digest_count; i++)
        if (s->digests[i] == digest) return 0;
    if (s->digest_count == s->digest_capacity) {
        const size_t cap = s->digest_capacity ? s->digest_capacity * 2 : 64;
        uint64_t *more = realloc(s->digests, cap * sizeof *more);
        if (!more) return 1;                            /* no memory: keep the case rather than lose it */
        s->digests = more;
        s->digest_capacity = cap;
    }
    s->digests[s->digest_count++] = digest;
    return 1;
}

/* The digests of the cases already in the library for this function (earlier runs). It runs on a
   game thread, whose stack is small: the buffers come from the heap. */
static void load_library(capture_slot *s, const char *function)
{
    enum { PATH = 4096, CHUNK = 65536 };
    const char *dir = getenv("BB_CAPTURE_DIR");
    char *path = malloc(PATH), *file = malloc(PATH), *chunk = malloc(CHUNK);
    DIR *d = path && file && chunk && dir && snprintf(path, PATH, "%s/%s", dir, function) < PATH ? opendir(path) : NULL;
    for (struct dirent *e; d && (e = readdir(d));) {
        const size_t len = strlen(e->d_name);
        if (len < 6 || strcmp(e->d_name + len - 5, ".json") || snprintf(file, PATH, "%s/%s", path, e->d_name) >= PATH)
            continue;
        FILE *f = fopen(file, "r");
        if (!f) continue;
        char *text = NULL;
        size_t size = 0;
        FILE *buffer = open_memstream(&text, &size);
        if (buffer) {
            for (size_t n; (n = fread(chunk, 1, CHUNK, f));) fwrite(chunk, 1, n, buffer);
            fclose(buffer);
            /* Cases end in a newline the writer adds; the digest is of the object alone. */
            if (size && text[size - 1] == '\n') text[size - 1] = 0;
            if (text) add_digest(s, case_digest(text));
        }
        free(text);
        fclose(f);
    }
    if (d) closedir(d);
    free(path);
    free(file);
    free(chunk);
}

int rt_capture_keep(const char *function, const char *json)
{
    if (!function || !json) return 0;
    const int index = slot_for(function);
    if (index < 0) return 1;
    capture_slot *s = &slots[index];
    while (atomic_flag_test_and_set(&s->lock)) {}
    if (!s->loaded) {
        load_library(s, function);
        s->loaded = 1;
    }
    const int fresh = add_digest(s, case_digest(json));
    atomic_flag_clear(&s->lock);
    if (fresh) atomic_fetch_add(&s->kept, 1);
    return fresh;
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
