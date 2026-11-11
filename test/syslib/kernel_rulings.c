/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The answers the maintainer ruled on (2026-11-10; the default CPU mask revised 2026-11-11) for
   the libkernel calls and data libc.elf and libSceFios2 import
   (runtime/syslib/kernel_libc_support.c, kernel_thread.c), the disc map's unnamed import
   (runtime/syslib/disc_map.c, registered by its NID) and a module loaded twice
   (runtime/syslib/kernel_sysmodule.c). */
#include "../syslib_test.h"

#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*void_fn)(void);
typedef int64_t (*read_fn)(int32_t, void *, uint64_t);
typedef int32_t (*affinity_fn)(void *, uint64_t *);
typedef int32_t (*set_affinity_fn)(void *, uint64_t);
typedef void *(*self_fn)(void);
typedef int32_t (*disc_fn)(const char *, int64_t, int64_t, int32_t *, int32_t *, int32_t *);
typedef int32_t (*module_fn)(uint32_t);

static read_fn read_;
static affinity_fn get_affinity;
static disc_fn disc;

static void read_other_descriptor(void) { char c; read_(3, &c, 1); }
static void affinity_no_mask(void) { get_affinity(((self_fn)fn("libkernel", "scePthreadSelf"))(), NULL); }
static void disc_no_path(void) { int32_t a, b, c; disc(NULL, 0, 1, &a, &b, &c); }
static void disc_no_variable(void) { int32_t a, b; disc("/app0/x", 0, 1, &a, &b, NULL); }

SYSLIB_TEST(kernel_rulings)
{
    const void_fn getpid_ = (void_fn)fn("libkernel", "getpid");
    const uint64_t *guard = (const uint64_t *)fn("libkernel", "__stack_chk_guard");
    read_ = (read_fn)fn("libkernel", "_read");
    get_affinity = (affinity_fn)fn("libkernel", "scePthreadGetaffinity");
    const set_affinity_fn set_affinity = (set_affinity_fn)fn("libkernel", "scePthreadSetaffinity");
    disc = (disc_fn)fn("libSceDiscMap", "ioKMruft1ek");
    CHECK(is_implemented("libkernel", "getpid") && is_implemented("libkernel", "__stack_chk_guard"));
    CHECK(is_implemented("libkernel", "_read") && is_implemented("libkernel", "scePthreadGetaffinity"));
    CHECK(is_implemented("libSceDiscMap", "ioKMruft1ek"));
    if (!getpid_ || !guard || !read_ || !get_affinity || !set_affinity || !disc) return;

    CHECK(getpid_() == 1000);
    CHECK(*guard == UINT64_C(0xdeadbeef54321abc));

    char buffer[4] = {'x', 'x', 'x', 'x'};
    CHECK(read_(0, buffer, sizeof buffer) == 0 && buffer[0] == 'x'); /* end of file, nothing written */
    CHECK(died_in_fatal(read_other_descriptor));

    /* A thread nobody set a mask on answers 0x3f, the six CPUs 0 to 5; a set mask is answered back. */
    void *self = ((self_fn)fn("libkernel", "scePthreadSelf"))();
    uint64_t mask = 0;
    if (self) {
        CHECK(get_affinity(self, &mask) == 0 && mask == 0x3f);
        CHECK(set_affinity(self, 0x3c) == 0);
        CHECK(get_affinity(self, &mask) == 0 && mask == 0x3c);
        CHECK(died_in_fatal(affinity_no_mask));
        set_affinity(self, 0x3f); /* back to the six CPUs */
    }
    CHECK(get_affinity(NULL, &mask) == (int32_t)0x80020003u); /* ESRCH */

    /* The disc map's unnamed import: registered by its NID, "no bitmap info", nothing written. */
    const rt_syslib_entry *entry = NULL;
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, "libSceDiscMap") && !strcmp(e->symbol, "ioKMruft1ek")) entry = e;
    CHECK(entry && entry->by_nid == 1);
    int32_t a = 7, b = 7, c = 7;
    CHECK(disc("/app0/dvdroot/data.bdt", 0x1000, 0x8000, &a, &b, &c) == (int32_t)0x81100004u);
    CHECK(a == 7 && b == 7 && c == 7);
    CHECK(died_in_fatal(disc_no_path));
    CHECK(died_in_fatal(disc_no_variable));
}
