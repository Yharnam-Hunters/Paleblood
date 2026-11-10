/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sigprocmask and sigfillset (libkernel) and scePthreadRename (runtime/syslib/kernel_thread.c). */
#define _GNU_SOURCE /* pthread_getname_np */
#include "../syslib_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef int (*f_sigmask)(int, const uint32_t *, uint32_t *);
typedef int (*f_fillset)(uint32_t *);
typedef int (*f_rename)(void *, const char *);
typedef void *(*f_self)(void);
typedef int (*f_attr)(void **);
typedef int (*f_attr_u64)(void **, uint64_t);
typedef int (*f_attr_int)(void **, int);
typedef int (*f_create)(void **, void **, void *(*)(void *), void *, const char *);
typedef int (*f_join)(void *, void **);

static f_sigmask sigmask;
static f_fillset fillset;
static f_rename rename_thread;
static f_self self;
static atomic_int gate;
static char worker_name[16];

static void *renamed_worker(void *argument)
{
    (void)argument;
    for (int ms = 0; ms < 5000 && !atomic_load(&gate); ms++) usleep(1000);
    pthread_getname_np(pthread_self(), worker_name, sizeof worker_name);
    return NULL;
}
static void *returns(void *argument) { return argument; }

static void fill_nothing(void) { fillset(NULL); }
static void mask_unknown_how(void) { const uint32_t set[4] = {1, 0, 0, 0}; sigmask(9, set, NULL); }
static void rename_unknown(void) { static uint64_t not_a_thread[64]; rename_thread(not_a_thread, "x"); }
static void rename_no_name(void) { rename_thread(self(), NULL); }
static void *collected_thread;
static void rename_collected(void) { rename_thread(collected_thread, "late"); }

SYSLIB_TEST(signals_rename)
{
    sigmask = (f_sigmask)fn("libkernel", "sigprocmask");
    fillset = (f_fillset)fn("libkernel", "sigfillset");
    rename_thread = (f_rename)fn("libkernel", "scePthreadRename");
    self = (f_self)fn("libkernel", "scePthreadSelf");
    const f_attr a_init = (f_attr)fn("libkernel", "scePthreadAttrInit");
    const f_attr_u64 a_stack = (f_attr_u64)fn("libkernel", "scePthreadAttrSetstacksize");
    const f_attr_int a_detach = (f_attr_int)fn("libkernel", "scePthreadAttrSetdetachstate");
    const f_create create = (f_create)fn("libkernel", "scePthreadCreate");
    const f_join join = (f_join)fn("libkernel", "scePthreadJoin");
    CHECK(is_implemented("libkernel", "sigprocmask") && is_implemented("libkernel", "sigfillset"));
    CHECK(is_implemented("libkernel", "scePthreadRename"));
    if (!sigmask || !fillset || !rename_thread || !self || !a_init || !a_stack || !a_detach || !create || !join) return;

    /* sigfillset: all four words; sigprocmask: the same mask as _sigprocmask, SIGKILL and SIGSTOP never blocked. */
    uint32_t full[4] = {0, 0, 0, 0}, old[4], now[4];
    CHECK(fillset(full) == 0 && full[0] == ~0u && full[1] == ~0u && full[2] == ~0u && full[3] == ~0u);
    CHECK(sigmask(3, full, old) == 0 && sigmask(1, NULL, now) == 0);
    CHECK(now[0] == ~((1u << 8) | (1u << 16)) && now[1] == ~0u && now[2] == ~0u && now[3] == ~0u);
    CHECK(sigmask(3, old, NULL) == 0 && sigmask(1, NULL, now) == 0 && !memcmp(now, old, sizeof old));
    CHECK(died_in_fatal(fill_nothing) && died_in_fatal(mask_unknown_how));

    /* scePthreadRename: the calling thread and another one, the host thread named with 15 bytes. */
    char before[16] = "", name[16] = "";
    pthread_getname_np(pthread_self(), before, sizeof before);
    CHECK(rename_thread(self(), "a thread name longer than thirty-one bytes") == 0);
    CHECK(!pthread_getname_np(pthread_self(), name, sizeof name) && !strcmp(name, "a thread name l"));
    pthread_setname_np(pthread_self(), before);
    void *attr = NULL, *worker = NULL;
    CHECK(a_init(&attr) == 0 && a_stack(&attr, 0x10000) == 0 && a_detach(&attr, 0) == 0);
    atomic_store(&gate, 0);
    CHECK(create(&worker, &attr, renamed_worker, NULL, "worker") == 0);
    CHECK(rename_thread(worker, "job-3") == 0);
    atomic_store(&gate, 1);
    CHECK(join(worker, NULL) == 0 && !strcmp(worker_name, "job-3"));

    CHECK(create(&collected_thread, &attr, returns, NULL, "collected") == 0 && join(collected_thread, NULL) == 0);
    CHECK(died_in_fatal(rename_unknown) && died_in_fatal(rename_no_name) && died_in_fatal(rename_collected));
}
