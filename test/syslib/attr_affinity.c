/* SPDX-License-Identifier: GPL-2.0-or-later */
/* scePthreadAttrGetaffinity (runtime/syslib/kernel_thread.c): a fresh attribute answers 0x3f, the
   six CPUs 0 to 5 (the maintainer's ruling of 2026-11-11), as does scePthreadGetaffinity on a new
   thread with no mask; a mask set on it or copied from a thread by scePthreadAttrGet is answered
   back, exactly eight bytes are written, and the bad arguments. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*attr_fn)(void **);
typedef int32_t (*attr_mask_fn)(void **, uint64_t);
typedef int32_t (*attr_int_fn)(void **, int);
typedef int32_t (*attr_get_mask_fn)(void **, uint64_t *);
typedef int32_t (*attr_get_fn)(void *, void **);
typedef int32_t (*thread_mask_fn)(void *, uint64_t);
typedef int32_t (*thread_get_mask_fn)(void *, uint64_t *);
typedef void *(*self_fn)(void);
typedef void *(*entry_fn)(void *);
typedef int32_t (*create_fn)(void **, void **, entry_fn, void *, const char *);
typedef int32_t (*join_fn)(void *, void **);

#define SCE_EINVAL ((int32_t)0x80020016u)
#define DEFAULT_CPU_MASK UINT64_C(0x3f) /* the answer when the guest set no mask */

static attr_fn attr_init, attr_destroy;
static attr_mask_fn attr_set_mask, attr_set_stack;
static attr_get_mask_fn attr_get_mask;
static attr_int_fn attr_set_detach;
static attr_get_fn attr_get;
static thread_get_mask_fn thread_get_mask;
static self_fn self;

static void get_mask_no_variable(void)
{
    void *attr = NULL;
    attr_init(&attr);
    attr_get_mask(&attr, NULL);
}

/* The mask scePthreadAttrGet copies from the calling thread, as libc.elf's thread-count probe asks. */
static uint64_t copied_mask(void)
{
    void *attr = NULL;
    uint64_t mask = 0;
    if (attr_init(&attr) || attr_get(self(), &attr) || attr_get_mask(&attr, &mask)) mask = 0;
    attr_destroy(&attr);
    return mask;
}

static void *report_copied_mask(void *answer)
{
    *(uint64_t *)answer = copied_mask();
    return NULL;
}

/* scePthreadGetaffinity on the calling thread, which a new thread created without a mask asks. */
static void *report_own_mask(void *answer)
{
    if (thread_get_mask(self(), (uint64_t *)answer)) *(uint64_t *)answer = 0;
    return NULL;
}

/* Runs report_copied_mask on a new thread created with the mask (0: none set). */
static uint64_t mask_seen_by_thread(create_fn create, join_fn join, uint64_t mask)
{
    void *attr = NULL, *thread = NULL;
    uint64_t answer = 0;
    if (attr_init(&attr) || attr_set_stack(&attr, 0x10000) || attr_set_detach(&attr, 0)) return 0;
    if (mask) CHECK(attr_set_mask(&attr, mask) == 0);
    if (create(&thread, &attr, report_copied_mask, &answer, "attr_affinity") == 0) join(thread, NULL);
    else CHECK(0);
    attr_destroy(&attr);
    return answer;
}

SYSLIB_TEST(attr_affinity)
{
    attr_init = (attr_fn)fn("libkernel", "scePthreadAttrInit");
    attr_destroy = (attr_fn)fn("libkernel", "scePthreadAttrDestroy");
    attr_set_mask = (attr_mask_fn)fn("libkernel", "scePthreadAttrSetaffinity");
    attr_set_stack = (attr_mask_fn)fn("libkernel", "scePthreadAttrSetstacksize");
    attr_set_detach = (attr_int_fn)fn("libkernel", "scePthreadAttrSetdetachstate");
    attr_get_mask = (attr_get_mask_fn)fn("libkernel", "scePthreadAttrGetaffinity");
    attr_get = (attr_get_fn)fn("libkernel", "scePthreadAttrGet");
    thread_get_mask = (thread_get_mask_fn)fn("libkernel", "scePthreadGetaffinity");
    self = (self_fn)fn("libkernel", "scePthreadSelf");
    const thread_mask_fn thread_set_mask = (thread_mask_fn)fn("libkernel", "scePthreadSetaffinity");
    const create_fn create = (create_fn)fn("libkernel", "scePthreadCreate");
    const join_fn join = (join_fn)fn("libkernel", "scePthreadJoin");
    CHECK(is_implemented("libkernel", "scePthreadAttrGetaffinity"));
    if (!attr_init || !attr_destroy || !attr_set_mask || !attr_set_stack || !attr_set_detach || !attr_get_mask || !attr_get ||
        !thread_get_mask || !self || !thread_set_mask || !create || !join)
        return;

    /* A fresh attribute: the default mask, written as exactly eight bytes between canaries. */
    void *attr = NULL;
    CHECK(attr_init(&attr) == 0);
    struct { uint64_t before, mask, after; } out;
    memset(&out, 0xa5, sizeof out);
    out.mask = 0;
    CHECK(attr_get_mask(&attr, &out.mask) == 0 && out.mask == DEFAULT_CPU_MASK);
    CHECK(out.before == UINT64_C(0xa5a5a5a5a5a5a5a5) && out.after == UINT64_C(0xa5a5a5a5a5a5a5a5));

    /* A mask set on the attribute is answered back, and a later one replaces it. */
    uint64_t mask = 0;
    CHECK(attr_set_mask(&attr, 0x3c) == 0);
    CHECK(attr_get_mask(&attr, &mask) == 0 && mask == 0x3c);
    CHECK(attr_set_mask(&attr, UINT64_C(0x8000000000000001)) == 0);
    CHECK(attr_get_mask(&attr, &mask) == 0 && mask == UINT64_C(0x8000000000000001));

    /* scePthreadAttrGet replaces the attribute, mask included: a thread with no mask gives the default. */
    CHECK(mask_seen_by_thread(create, join, 0) == DEFAULT_CPU_MASK);
    CHECK(mask_seen_by_thread(create, join, 0x5) == 0x5);
    void *thread_attr = NULL;
    CHECK(attr_init(&thread_attr) == 0 && attr_set_stack(&thread_attr, 0x10000) == 0 &&
          attr_set_detach(&thread_attr, 0) == 0);
    CHECK(attr_set_mask(&thread_attr, 0x30) == 0);
    void *thread = NULL;
    uint64_t answer = 0;
    CHECK(create(&thread, &thread_attr, report_copied_mask, &answer, "attr_affinity") == 0);
    if (thread) join(thread, NULL);
    CHECK(answer == 0x30);
    attr_destroy(&thread_attr);

    /* A new thread nobody set a mask on answers the default from scePthreadGetaffinity too. */
    CHECK(attr_init(&thread_attr) == 0 && attr_set_stack(&thread_attr, 0x10000) == 0 &&
          attr_set_detach(&thread_attr, 0) == 0);
    thread = NULL;
    answer = 0;
    CHECK(create(&thread, &thread_attr, report_own_mask, &answer, "attr_affinity") == 0);
    if (thread) join(thread, NULL);
    CHECK(answer == DEFAULT_CPU_MASK);
    attr_destroy(&thread_attr);

    /* A mask the thread set on itself later is the one copied. */
    void *const main_thread = self();
    if (main_thread) {
        CHECK(thread_set_mask(main_thread, 0x2) == 0);
        CHECK(copied_mask() == 0x2);
        thread_set_mask(main_thread, DEFAULT_CPU_MASK); /* back to the six CPUs */
    }

    /* A variable holding no attribute is EINVAL; no variable for the mask stops by name. */
    void *none = NULL;
    mask = 7;
    CHECK(attr_get_mask(&none, &mask) == SCE_EINVAL && mask == 7);
    CHECK(attr_get_mask(NULL, &mask) == SCE_EINVAL && mask == 7);
    CHECK(died_in_fatal(get_mask_no_variable));
    attr_destroy(&attr);
}
