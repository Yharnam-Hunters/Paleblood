/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What libc.elf and libSceFios2 import for start-up, output, clocks, memory and exceptions
   (runtime/syslib/kernel_libc_support.c, kernel_memory.c, files.c, libc_internal.c). */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int32_t *(*errno_fn)(void);
typedef void (*ptr_void_fn)(void *);
typedef int32_t (*uptr_fn)(uintptr_t);
typedef int64_t (*write_fn)(int32_t, const void *, uint64_t);
typedef int32_t (*rusage_fn)(int32_t, void *);
typedef uintptr_t (*signal_fn)(int32_t, uintptr_t);
typedef int32_t (*tz_fn)(int32_t *);
typedef int32_t (*raise_fn)(uint32_t, int64_t);
typedef void *(*mmap_fn)(void *, uint64_t, int32_t, int32_t, int32_t, int64_t);
typedef int32_t (*munmap_fn)(void *, uint64_t);
typedef int32_t (*msync_fn)(void *, uint64_t, int32_t);
typedef int32_t (*named_fn)(void **, uint64_t, int32_t, int32_t, const char *);
typedef int32_t (*mlock_fn)(const void *, uint64_t);
typedef int32_t (*open_fn)(const char *, int32_t, uint32_t);
typedef int32_t (*close_fn)(int32_t);
typedef int32_t (*fstat_fn)(int32_t, void *);
typedef int32_t *(*localtime_fn)(const int64_t *);
typedef int64_t (*mktime_fn)(int32_t *);
typedef void *(*malloc_fn)(uint64_t);
typedef int32_t (*fflush_fn)(void *);

static write_fn write_;
static rusage_fn rusage;
static tz_fn timezone_;
static raise_fn raise_, raise_release;
static mmap_fn mmap_;
static fflush_fn fflush_;

static void write_fd_3(void) { write_(3, "x", 1); }
static void rusage_children(void)
{
    uint8_t usage[144];
    rusage(-1, usage);
}
static void timezone_null(void) { timezone_(NULL); }
static void raise_abort(void) { raise_(0xa002000bu, 0); }
static void raise_release_abort(void) { raise_release(0xa002000cu, 0); }
static void mmap_file(void) { mmap_(NULL, 0x4000, 1, 2, 5, 0); }
static void mmap_hint(void) { mmap_((void *)0x10000000, 0x4000, 3, 0x1002, -1, 0); }
static void mmap_shared(void) { mmap_(NULL, 0x4000, 3, 0x1001, -1, 0); }
static void mmap_read_only(void) { mmap_(NULL, 0x4000, 1, 0x1002, -1, 0); }
static localtime_fn localtime_entry;
static mktime_fn mktime_entry;
static void localtime_null(void) { localtime_entry(NULL); }
static void mktime_null(void) { mktime_entry(NULL); }
static void fflush_other(void)
{
    char not_a_stream[8];
    fflush_(not_a_stream);
}

SYSLIB_TEST(kernel_libc_support)
{
    const errno_fn guest_errno = fn("libkernel", "__error");
    const char *const *progname = fn("libkernel", "__progname");
    const ptr_void_fn cxa = fn("libkernel", "__pthread_cxa_finalize");
    const uptr_fn signal_return = fn("libkernel", "_is_signal_return");
    write_ = fn("libkernel", "_write");
    rusage = fn("libkernel", "getrusage");
    const signal_fn signal_ = fn("libkernel", "signal");
    timezone_ = fn("libkernel", "sceKernelGettimezone");
    raise_ = fn("libkernel", "sceKernelDebugRaiseException");
    raise_release = fn("libkernel", "sceKernelDebugRaiseExceptionOnReleaseMode");
    CHECK(is_implemented("libkernel", "__progname") && is_implemented("libkernel", "signal"));
    if (!guest_errno || !progname || !cxa || !signal_return || !write_ || !rusage || !signal_ || !timezone_ || !raise_ || !raise_release) return;

    CHECK(*progname && !strcmp(*progname, "eboot.bin"));
    cxa(NULL);
    CHECK(signal_return(0x1234) == 0);
    CHECK(write_(1, "", 0) == 0 && write_(2, "", 0) == 0 && died_in_fatal(write_fd_3));
    int64_t usage[18];
    memset(usage, 0xff, sizeof usage);
    CHECK(rusage(0, usage) == 0 && usage[0] >= 0 && usage[1] >= 0 && usage[1] < 1000000 && usage[3] < 1000000 && usage[4] > 0);
    CHECK(died_in_fatal(rusage_children));
    /* signal: the previous disposition; SIG_ERR and EINVAL outside the rules. */
    CHECK(signal_(9, 0) == 0 && signal_(15, 0x1234) == 0 && signal_(15, 0) == 0x1234);
    *guest_errno() = 0;
    CHECK(signal_(0, 0) == (uintptr_t)-1 && *guest_errno() == 22);
    CHECK(signal_(129, 0) == (uintptr_t)-1 && signal_(128, 0) == 0);
    CHECK(signal_(9, 0x1234) == (uintptr_t)-1 && signal_(17, 0x1234) == (uintptr_t)-1 && signal_(17, 0) == 0);
    /* The time zone: minutes east of UTC plus daylight saving minutes, as the host's. */
    int32_t zone[2] = {77, 77};
    CHECK(timezone_(zone) == 0);
    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    CHECK(zone[0] + zone[1] == (int32_t)(local.tm_gmtoff / 60) && (zone[1] == 0 || zone[1] == 60) && (zone[1] == 60) == (local.tm_isdst > 0));
    CHECK(died_in_fatal(timezone_null));
    CHECK(raise_(0xa002000bu, 1) == (int32_t)0x80020016u && raise_release(1, 1) == (int32_t)0x80020016u);
    CHECK(died_in_fatal(raise_abort) && died_in_fatal(raise_release_abort));

    /* Memory. */
    mmap_ = fn("libkernel", "mmap");
    const munmap_fn munmap_ = fn("libkernel", "munmap");
    const msync_fn msync_ = fn("libkernel", "msync");
    const named_fn named = fn("libkernel", "sceKernelMapNamedFlexibleMemory");
    const mlock_fn mlock_ = fn("libkernel", "sceKernelMlock");
    if (!mmap_ || !munmap_ || !msync_ || !named || !mlock_) return;
    uint8_t *const block = mmap_(NULL, 0x5000, 3, 0x1002, -1, 0);
    CHECK(block && block != (void *)-1 && ((uintptr_t)block & 0x3fff) == 0);
    if (block && block != (void *)-1) block[0x7fff] = 1; /* rounded up to two 16 KiB pages, writable */
    CHECK(msync_(block + 0x1000, 0x1000, 1) == 0);
    *guest_errno() = 0;
    CHECK(mmap_(NULL, 0, 3, 0x1002, -1, 0) == (void *)-1 && *guest_errno() == 22);
    CHECK(mmap_(NULL, 0x4000, 3, 0x1002, 3, 0) == (void *)-1 && mmap_(NULL, 0x4000, 3, 0x1002, -1, 8) == (void *)-1);
    CHECK(died_in_fatal(mmap_file) && died_in_fatal(mmap_hint) && died_in_fatal(mmap_shared) && died_in_fatal(mmap_read_only));
    *guest_errno() = 0;
    CHECK(munmap_(block, 0) == -1 && *guest_errno() == 22);
    CHECK(munmap_(block, 0x8000) == 0);
    *guest_errno() = 0;
    CHECK(msync_(block, 0x1000, 1) == -1 && *guest_errno() == 12); /* no longer mapped */
    void *named_block = NULL;
    CHECK(named(&named_block, 0x4000, 3, 0, NULL) == (int32_t)0x8002000eu);
    CHECK(named(&named_block, 0x4000, 3, 0, "a-name-that-is-longer-than-31-characters") == (int32_t)0x8002003fu);
    CHECK(named(&named_block, 0x4000, 3, 0, "SceLibcHeap") == 0 && named_block && munmap_(named_block, 0x4000) == 0);
    CHECK(mlock_(&usage, 8) == 0 && mlock_(NULL, 8) == (int32_t)0x80020016u && mlock_(&usage, 0) == (int32_t)0x80020016u);
    CHECK(mlock_((void *)-16, 32) == (int32_t)0x80020016u);

    /* fstat. */
    const open_fn kopen = fn("libkernel", "sceKernelOpen");
    const close_fn kclose = fn("libkernel", "sceKernelClose");
    const fstat_fn fstat_ = fn("libkernel", "fstat");
    if (!kopen || !kclose || !fstat_) return;
    char folder[] = "/tmp/pb-libc-support-XXXXXX", path[64];
    CHECK(mkdtemp(folder) != NULL);
    rt_process.data_dir = folder;
    snprintf(path, sizeof path, "%s/f", folder);
    FILE *file = fopen(path, "wb");
    if (file) fputs("hello", file), fclose(file);
    const int32_t fd = kopen("/data/f", 0, 0);
    uint8_t st[120];
    int64_t size = 0;
    CHECK(fd >= 3 && fstat_(fd, st) == 0);
    memcpy(&size, st + 0x48, 8);
    CHECK(size == 5);
    *guest_errno() = 0;
    CHECK(fstat_(1000, st) == -1 && *guest_errno() == 9);
    kclose(fd);
    unlink(path);
    rmdir(folder);
    rt_process.data_dir = NULL;

    /* libSceLibcInternal. */
    const localtime_fn localtime_ = fn("libSceLibcInternal", "localtime");
    const mktime_fn mktime_ = fn("libSceLibcInternal", "mktime");
    const malloc_fn malloc_ = fn("libSceLibcInternal", "malloc");
    const ptr_void_fn free_ = fn("libSceLibcInternal", "free");
    fflush_ = fn("libSceLibcInternal", "fflush");
    const ptr_void_fn cxa_finalize = fn("libSceLibcInternal", "__cxa_finalize");
    void *const stdout_object = fn("libSceLibcInternal", "_Stdout");
    const int32_t *const need = fn("libSceLibcInternal", "Need_sceLibcInternal");
    if (!localtime_ || !mktime_ || !malloc_ || !free_ || !fflush_ || !cxa_finalize || !stdout_object || !need) return;
    const int64_t now64 = (int64_t)now;
    const int32_t *parts = localtime_(&now64);
    CHECK(parts && parts[0] == local.tm_sec && parts[2] == local.tm_hour && parts[3] == local.tm_mday && parts[5] == local.tm_year &&
          parts[8] == local.tm_isdst);
    int32_t tm_parts[9] = {parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], 0, 0, -1};
    CHECK(mktime_(tm_parts) == now64);
    int32_t overflow[9] = {0, 0, 12, 32, 0, 120, 0, 0, -1}; /* 32 January 2020 is 1 February */
    CHECK(mktime_(overflow) > 0 && overflow[3] == 1 && overflow[4] == 1 && overflow[7] == 31);
    int32_t too_far[9] = {0, 0, 0, 1, 0x7fffffff, 0x7fffffff, 0, 0, -1};
    CHECK(mktime_(too_far) == -1 && too_far[5] == 0x7fffffff && too_far[6] == 0); /* left as it was */
    localtime_entry = localtime_, mktime_entry = mktime_;
    CHECK(died_in_fatal(localtime_null) && died_in_fatal(mktime_null));
    uint8_t *const memory = malloc_(64);
    CHECK(memory && ((uintptr_t)memory & 15) == 0);
    if (memory) memset(memory, 1, 64);
    free_(memory);
    CHECK(fflush_(stdout_object) == 0 && fflush_(NULL) == 0 && died_in_fatal(fflush_other));
    CHECK(*need == 1);
    cxa_finalize(NULL);
}
