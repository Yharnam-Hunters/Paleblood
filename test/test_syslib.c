/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The runtime's system library implementations, called the way the guest calls them: through the
   RT_SYSLIB registry, by library and symbol. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int unresolved_test_entry(void) { return -1; }
RT_SYSLIB_UNRESOLVED("test", testUnresolvedEntry, unresolved_test_entry);

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void *fn(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, library) && !strcmp(e->symbol, symbol)) return e->fn;
    fprintf(stderr, "no %s %s\n", library, symbol);
    failures++;
    return NULL;
}

static int is_implemented(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, library) && !strcmp(e->symbol, symbol))
            return e->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED;
    return 0;
}

typedef int (*f1)(void **);
typedef int (*f2i)(void **, int);
typedef int (*f_state)(int *);
typedef int (*f_check)(void);
typedef int (*f_register)(void *, void *, int *);
typedef int (*f_unregister)(int);
typedef int (*f_info)(int, void *);
typedef int (*f_nat)(void *);
typedef int (*finit)(void **, void **, const char *);
typedef int (*f_equal)(void *, void *);
typedef int (*f_attr_get)(void *, void **);
typedef uint32_t (*f_u32_u32)(uint32_t);
typedef uint16_t (*f_u16_u16)(uint16_t);
typedef int (*f_inet_pton)(int, const char *, void *);
typedef int32_t (*f_sysmodule)(uint16_t);

static void *net_inet_pton_entry;
static int net_pton_unknown_family(void)
{
    unsigned char address[4];
    return ((f_inet_pton)net_inet_pton_entry)(0x7f, "192.0.2.1", address);
}
static int net_pton_null_source(void)
{
    unsigned char address[4];
    return ((f_inet_pton)net_inet_pton_entry)(2, NULL, address);
}
static int net_pton_null_destination(void)
{
    return ((f_inet_pton)net_inet_pton_entry)(2, "192.0.2.1", NULL);
}
static void net_pton_unknown_family_call(void) { (void)net_pton_unknown_family(); }
static void net_pton_null_source_call(void) { (void)net_pton_null_source(); }
static void net_pton_null_destination_call(void) { (void)net_pton_null_destination(); }

/* libSceNetCtl, the offline group (issue #41). rt_fatal aborts the process (runtime/process.c), so a
   fatal path can only be proved in a child: fork, call the entry the way the guest does, and check
   the signal it died on. netctl_shared is MAP_SHARED and poisoned, so a child that wrote into the
   buffer it was handed would still be visible here even though the child never returns. */
static void *netctl_get_state_entry, *netctl_check_entry, *netctl_register_entry, *netctl_unregister_entry;
static void *netctl_get_info_entry, *netctl_get_nat_info_entry;
static char *netctl_shared;
static int netctl_callbacks_run;

static void netctl_must_not_run(void) { netctl_callbacks_run++; }

static int netctl_state(int *state) { return ((f_state)netctl_get_state_entry)(state); }
static int netctl_check(void) { return ((f_check)netctl_check_entry)(); }
static int netctl_register(void *callback, void *arg, int *id)
{
    return ((f_register)netctl_register_entry)(callback, arg, id);
}
static int netctl_unregister(int id) { return ((f_unregister)netctl_unregister_entry)(id); }
static int netctl_info(int module, void *info) { return ((f_info)netctl_get_info_entry)(module, info); }
static int netctl_nat(void *nat_info) { return ((f_nat)netctl_get_nat_info_entry)(nat_info); }

/* pthread_self on a host thread with no guest thread registered: the guest thread is per host
   thread, so a fresh host thread has none, and the call must stop by its own name. */
static void *(*posix_self_entry)(void);
static void *call_posix_self(void *unused)
{
    (void)unused;
    return posix_self_entry();
}
static void posix_self_on_unregistered_thread(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, call_posix_self, NULL) == 0) pthread_join(thread, NULL);
}

/* Condition variables, waited on by host threads: a waiter needs no guest thread, only the cond
   and mutex objects the SCE calls made. */
typedef int (*f_cond_wait)(void **, void **);
typedef int (*f_cond_timedwait)(void **, void **, uint32_t);
static f1 cond_lock, cond_unlock, cond_signal_entry;
static f_cond_wait cond_wait_entry;
static f_cond_timedwait cond_timedwait_entry;
static finit cond_init_entry;
static void *cond_shared, *cond_mutex, *cond_recursive_mutex;
static int cond_ready, cond_go, cond_woken;

/* Counts itself ready and waits for the go, all under the mutex: once the main thread holds the
   mutex and sees the count, every counted waiter is inside its wait. */
static void *cond_waiter(void *unused)
{
    (void)unused;
    cond_lock(&cond_mutex);
    cond_ready++;
    int r = 0;
    while (!cond_go && !r) r = cond_wait_entry(&cond_shared, &cond_mutex);
    if (!r) cond_woken++;
    cond_unlock(&cond_mutex);
    return NULL;
}

/* Gets the mutex only once the main thread's wait has released it, then signals. */
static void *cond_signaler(void *unused)
{
    (void)unused;
    cond_lock(&cond_mutex);
    cond_go = 1;
    cond_signal_entry(&cond_shared);
    cond_unlock(&cond_mutex);
    return NULL;
}

static void cond_wait_recursive(void) { cond_wait_entry(&cond_shared, &cond_recursive_mutex); }
static void cond_timedwait_recursive(void) { cond_timedwait_entry(&cond_shared, &cond_recursive_mutex, 1000); }
static void cond_init_with_attribute(void)
{
    void *cond = NULL, *attr = &cond; /* a variable holding some object */
    cond_init_entry(&cond, &attr, "cond");
}

static int died_in_fatal(void (*call)(void))
{
    const pid_t child = fork();
    if (child == 0) {
        call();
        _exit(1); /* the call returned, so the answer was not the fatal being proved */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

/* Microseconds elapsed since `start` on the host's monotonic clock: the sleeps are checked as
   lasting at least what was asked, never as an exact duration. */
static int64_t elapsed_us(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)(now.tv_sec - start->tv_sec) * 1000000 + (now.tv_nsec - start->tv_nsec) / 1000;
}

static int (*posix_usleep_entry)(uint32_t);
static void usleep_one_second(void) { posix_usleep_entry(1000000); }

/* The paths whose PS4 answer is not established. The ids are file-scope so a child can unregister
   one the parent registered before forking. */
static int netctl_id_a, netctl_id_b;
static void state_into_null(void) { netctl_state(NULL); }
static void register_null_callback(void) { netctl_register(NULL, NULL, &netctl_id_a); }
static void register_null_id(void) { netctl_register((void *)netctl_must_not_run, NULL, NULL); }
static void unregister_id_a(void) { netctl_unregister(netctl_id_a); }
static void unregister_unknown_id(void) { netctl_unregister(0x7fff); }
static void info_of_module_one(void) { netctl_info(1, netctl_shared); }
/* A size-prefixed buffer of the shape the guest's own sites build (16 at offset 0), on the child's
   stack: the fatal must not write through it, and netctl_shared stays the untouched witness. */
static void nat_info_with_size(void) { int nat[4] = {16, 0, 0, 0}; netctl_nat(nat); }

/* libSceSysmodule: a stub module (one the title ships but the runtime does not map) loads as done.
   The ids here are made up; the real ones come from the game's data through runtime/boot.c. */
enum { STUB_ID = 0x7f01, OTHER_STUB_ID = 0x7f02, NOT_A_STUB_ID = 0x7f03 };
static f_sysmodule sysmodule_load, sysmodule_is_loaded, sysmodule_unload;
static void load_not_a_stub(void) { sysmodule_load(NOT_A_STUB_ID); }
static void is_loaded_not_a_stub(void) { sysmodule_is_loaded(NOT_A_STUB_ID); }
static void unload_not_a_stub(void) { sysmodule_unload(NOT_A_STUB_ID); }
static void load_stub_again(void) { sysmodule_load(STUB_ID); }
static void is_loaded_other_stub(void) { sysmodule_is_loaded(OTHER_STUB_ID); }
static void unload_other_stub(void) { sysmodule_unload(OTHER_STUB_ID); }

/* Thread creation will read an attribute's detach state through rt_thread_attr_detach_state. A
   state the guest did not set is one the system supplies, which is not established, so the read
   must stop the process; each such case is read in a child. detach_reader_attr is the variable the
   child reads. */
static int detach_state_of(void **attr) { return rt_thread_attr_detach_state(attr, "test"); }
static void *detach_reader_attr;
static void read_detach_state(void) { (void)detach_state_of(&detach_reader_attr); }
static void read_detach_state_without_variable(void) { (void)detach_state_of(NULL); }

/* Direct memory: the calls as the guest makes them. The stops are file-scope so a child can make
   them on a range the parent allocated before forking. */
typedef size_t (*f_dm_size)(void);
typedef int32_t (*f_dm_allocate)(int64_t, int64_t, uint64_t, uint64_t, int32_t, int64_t *);
typedef int32_t (*f_dm_map)(void **, uint64_t, int32_t, int32_t, int64_t, uint64_t);
typedef int32_t (*f_dm_release)(int64_t, uint64_t);
static f_dm_allocate dm_allocate;
static f_dm_map dm_map;
static f_dm_release dm_release;
static int64_t dm_mapped, dm_unmapped;
enum { DM_LENGTH = 0x7400000, DM_ALIGN = 0x200000, DM_RW = 3, DM_PAGE = 0x4000 };
static void dm_allocate_too_much(void) { int64_t s; dm_allocate(0, 1ll << 33, 1ull << 33, DM_ALIGN, 0, &s); }
static void dm_allocate_no_room(void) { int64_t s; dm_allocate(0, DM_PAGE, 2 * DM_PAGE, DM_PAGE, 0, &s); }
static void dm_allocate_odd_length(void) { int64_t s; dm_allocate(0, 1ll << 30, DM_PAGE + 1, DM_PAGE, 0, &s); }
static void dm_allocate_odd_alignment(void) { int64_t s; dm_allocate(0, 1ll << 30, DM_PAGE, 3 * DM_PAGE, 0, &s); }
static void dm_allocate_unknown_type(void) { int64_t s; dm_allocate(0, 1ll << 30, DM_PAGE, DM_PAGE, 7, &s); }
static void dm_allocate_no_variable(void) { dm_allocate(0, 1ll << 30, DM_PAGE, DM_PAGE, 0, NULL); }
static void dm_map_with_flags(void) { void *a = NULL; dm_map(&a, DM_PAGE, DM_RW, 0x10, dm_unmapped, DM_PAGE); }
static void dm_map_with_hint(void) { void *a = (void *)0x10000000; dm_map(&a, DM_PAGE, DM_RW, 0, dm_unmapped, DM_PAGE); }
static void dm_map_gpu(void) { void *a = NULL; dm_map(&a, DM_PAGE, DM_RW | 0x10, 0, dm_unmapped, DM_PAGE); }
static void dm_map_unallocated(void) { void *a = NULL; dm_map(&a, DM_PAGE, DM_RW, 0, 1ll << 32, DM_PAGE); }
static void dm_map_past_range(void) { void *a = NULL; dm_map(&a, 2 * DM_PAGE, DM_RW, 0, dm_unmapped, DM_PAGE); }
static void dm_release_mapped(void) { dm_release(dm_mapped, DM_LENGTH); }
static void dm_release_part(void) { dm_release(dm_mapped, DM_PAGE); }
static void dm_release_unknown(void) { dm_release(1ll << 32, DM_PAGE); }

/* Kernel semaphores. Waiters are host threads; the stops are file-scope so a child can make them. */
typedef int32_t (*f_sema_create)(void **, const char *, uint32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_sema_one)(void *);
typedef int32_t (*f_sema_wait)(void *, int32_t, uint32_t *);
typedef int32_t (*f_sema_count)(void *, int32_t);
static f_sema_create sema_create;
static f_sema_one sema_delete;
static f_sema_wait sema_wait;
static f_sema_count sema_signal, sema_poll;
static void *sema_shared, *sema_prio;
static int sema_order[2], sema_served;
static pthread_mutex_t sema_order_lock = PTHREAD_MUTEX_INITIALIZER;
enum { SEMA_FIFO = 1, SEMA_PRIO = 2 };

static void *sema_waiter(void *tag)
{
    if (sema_wait(sema_shared, 1, NULL) == 0) {
        pthread_mutex_lock(&sema_order_lock);
        sema_order[sema_served++] = (int)(intptr_t)tag;
        pthread_mutex_unlock(&sema_order_lock);
    }
    return NULL;
}
/* How many waiters have been served, read under the lock their threads write it under (a plain
   read in a spin loop may be hoisted out of it by the compiler). */
static int sema_served_now(void)
{
    pthread_mutex_lock(&sema_order_lock);
    const int n = sema_served;
    pthread_mutex_unlock(&sema_order_lock);
    return n;
}
static void *sema_prio_waiter(void *unused) { (void)unused; sema_wait(sema_prio, 1, NULL); return NULL; }
/* The runtime's semaphore object, mirrored (runtime/syslib/kernel_sema.c) so a test can see how
   many waiters are inside the wait before it signals; keep the two in step. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int32_t count, max;
    uint32_t attr;
    uint64_t next_ticket, serving;
    int waiters;
} sema_mirror;
static int sema_waiters(void *s)
{
    sema_mirror *m = s;
    pthread_mutex_lock(&m->lock);
    const int n = m->waiters;
    pthread_mutex_unlock(&m->lock);
    return n;
}

static void sema_create_no_variable(void) { sema_create(NULL, "s", SEMA_FIFO, 0, 1, NULL); }
static void sema_create_bad_attr(void) { void *s; sema_create(&s, "s", 4, 0, 1, NULL); }
static void sema_create_option(void) { void *s; int opt = 0; sema_create(&s, "s", SEMA_FIFO, 0, 1, &opt); }
static void sema_create_initial_over_max(void) { void *s; sema_create(&s, "s", SEMA_FIFO, 2, 1, NULL); }
static void sema_wait_with_timeout(void) { uint32_t t = 1000; sema_wait(sema_shared, 1, &t); }
static void sema_wait_null(void) { sema_wait(NULL, 1, NULL); }
static void sema_signal_past_max(void) { sema_signal(sema_shared, 0x10000); }
static void sema_poll_empty(void) { sema_poll(sema_shared, 1); }
static void sema_second_prio_waiter(void)
{
    pthread_t first;
    pthread_create(&first, NULL, sema_prio_waiter, NULL);
    while (sema_waiters(sema_prio) < 1) sched_yield();
    sema_wait(sema_prio, 1, NULL);
}

/* The time of day: a timeval of two 64-bit fields, seconds and microseconds. */
typedef struct {
    int64_t seconds, microseconds;
} tod_timeval;
typedef int (*f_tod)(tod_timeval *, const void *);
typedef int32_t (*f_sce_tod)(tod_timeval *);
static f_tod tod_posix;
static f_sce_tod tod_sce;
static void tod_with_zone(void) { tod_timeval tv; int zone[2] = {0, 0}; tod_posix(&tv, zone); }
static void tod_sce_null(void) { tod_sce(NULL); }

int main(void)
{
    /* Unresolved entries are still callable diagnostics, but are not reported as implemented. */
    CHECK(((int (*)(void))fn("test", "testUnresolvedEntry"))() == -1);
    CHECK(!is_implemented("test", "testUnresolvedEntry"));

    const finit init = (finit)fn("libkernel", "scePthreadMutexInit");
    const f1 lock = (f1)fn("libkernel", "scePthreadMutexLock"), trylock = (f1)fn("libkernel", "scePthreadMutexTrylock");
    const f1 unlock = (f1)fn("libkernel", "scePthreadMutexUnlock"), destroy = (f1)fn("libkernel", "scePthreadMutexDestroy");
    const f1 ainit = (f1)fn("libkernel", "scePthreadMutexattrInit"), adestroy = (f1)fn("libkernel", "scePthreadMutexattrDestroy");
    const f2i settype = (f2i)fn("libkernel", "scePthreadMutexattrSettype");
    const f1 plock = (f1)fn("libScePosix", "pthread_mutex_lock");
    const f_u32_u32 htonl = (f_u32_u32)fn("libSceNet", "sceNetHtonl");
    const f_u16_u16 htons = (f_u16_u16)fn("libSceNet", "sceNetHtons");
    const f_u32_u32 ntohl = (f_u32_u32)fn("libSceNet", "sceNetNtohl");
    const f_u16_u16 ntohs = (f_u16_u16)fn("libSceNet", "sceNetNtohs");
    net_inet_pton_entry = fn("libSceNet", "sceNetInetPton");
    if (failures) return 1;

    /* default (error-checking): relocking is EDEADLK (FreeBSD 11), as an SCE code */
    void *m = NULL;
    CHECK(init(&m, NULL, "m") == 0 && m);
    CHECK(lock(&m) == 0);
    CHECK(lock(&m) == (int)0x8002000b);
    CHECK(plock(&m) == 11);                       /* the POSIX name returns the errno itself */
    CHECK(unlock(&m) == 0);
    CHECK(unlock(&m) == (int)0x80020001);         /* not the owner: EPERM */
    CHECK(destroy(&m) == 0 && m == (void *)2);
    CHECK(lock(&m) == (int)0x80020016);           /* destroyed: EINVAL */

    /* a static initializer (NULL) becomes a mutex on first lock */
    void *s = NULL;
    CHECK(unlock(&s) == (int)0x80020001);
    CHECK(lock(&s) == 0 && s && s != (void *)1 && s != (void *)2);
    CHECK(unlock(&s) == 0);
    CHECK(destroy(&s) == 0);

    /* recursive through an attribute */
    void *a = NULL, *r = NULL;
    CHECK(ainit(&a) == 0 && a);
    CHECK(settype(&a, 5) == (int)0x80020016);
    CHECK(settype(&a, 2) == 0);
    CHECK(init(&r, &a, NULL) == 0);
    CHECK(lock(&r) == 0 && lock(&r) == 0 && trylock(&r) == 0);
    CHECK(unlock(&r) == 0 && unlock(&r) == 0 && unlock(&r) == 0);
    CHECK(destroy(&r) == 0);
    CHECK(adestroy(&a) == 0 && a == NULL);

    /* Condition variables (SCE names): SCE codes for FreeBSD libthr's errors, and a timed wait
       that takes microseconds from now (the guest's call site and OpenOrbis agree on that). */
    cond_init_entry = (finit)fn("libkernel", "scePthreadCondInit");
    const f1 cond_destroy = (f1)fn("libkernel", "scePthreadCondDestroy");
    const f1 cond_broadcast = (f1)fn("libkernel", "scePthreadCondBroadcast");
    cond_signal_entry = (f1)fn("libkernel", "scePthreadCondSignal");
    cond_wait_entry = (f_cond_wait)fn("libkernel", "scePthreadCondWait");
    cond_timedwait_entry = (f_cond_timedwait)fn("libkernel", "scePthreadCondTimedwait");
    cond_lock = lock;
    cond_unlock = unlock;
    if (failures) return 1;
    alarm(60); /* a wake that never comes kills the test with SIGALRM instead of hanging it */
    CHECK(cond_init_entry(&cond_shared, NULL, "cond") == 0 && cond_shared && cond_shared != (void *)1);
    CHECK(init(&cond_mutex, NULL, "cond mutex") == 0);
    CHECK(cond_signal_entry(&cond_shared) == 0 && cond_broadcast(&cond_shared) == 0); /* nobody to wake */

    /* A timeout is ETIMEDOUT, after at least the time asked, with the mutex held again. */
    struct timespec cond_start;
    CHECK(lock(&cond_mutex) == 0);
    clock_gettime(CLOCK_MONOTONIC, &cond_start);
    CHECK(cond_timedwait_entry(&cond_shared, &cond_mutex, 2000) == (int)0x8002003c);
    CHECK(elapsed_us(&cond_start) >= 2000);
    CHECK(cond_timedwait_entry(&cond_shared, &cond_mutex, 0) == (int)0x8002003c);
    CHECK(unlock(&cond_mutex) == 0);

    /* Waiting without owning the mutex is EPERM, also for one never locked (the static
       initializer, which stays as it is); a destroyed or missing mutex is EINVAL. */
    void *never_locked = NULL, *destroyed_mutex = (void *)2;
    CHECK(cond_wait_entry(&cond_shared, &cond_mutex) == (int)0x80020001);
    CHECK(cond_timedwait_entry(&cond_shared, &cond_mutex, 1000) == (int)0x80020001);
    CHECK(cond_wait_entry(&cond_shared, &never_locked) == (int)0x80020001 && never_locked == NULL);
    CHECK(cond_wait_entry(&cond_shared, &destroyed_mutex) == (int)0x80020016);
    CHECK(cond_wait_entry(&cond_shared, NULL) == (int)0x80020016);

    /* Signal wakes the waiting main thread, which has the mutex again when the wait returns. */
    pthread_t signaler;
    CHECK(lock(&cond_mutex) == 0);
    if (pthread_create(&signaler, NULL, cond_signaler, NULL)) { perror("pthread_create"); return 1; }
    int woke = 0;
    while (!cond_go && !woke) woke = cond_wait_entry(&cond_shared, &cond_mutex);
    CHECK(woke == 0 && cond_go == 1);
    CHECK(unlock(&cond_mutex) == 0);
    pthread_join(signaler, NULL);

    /* Broadcast wakes every waiter; while they wait, destroying the cond is EBUSY. */
    pthread_t waiters[2];
    cond_go = 0;
    for (int i = 0; i < 2; i++)
        if (pthread_create(&waiters[i], NULL, cond_waiter, NULL)) { perror("pthread_create"); return 1; }
    for (;;) {
        CHECK(lock(&cond_mutex) == 0);
        if (cond_ready == 2) break;
        CHECK(unlock(&cond_mutex) == 0);
        sched_yield();
    }
    CHECK(cond_destroy(&cond_shared) == (int)0x80020010);
    cond_go = 1;
    CHECK(cond_broadcast(&cond_shared) == 0);
    CHECK(unlock(&cond_mutex) == 0);
    for (int i = 0; i < 2; i++) pthread_join(waiters[i], NULL);
    CHECK(cond_woken == 2);

    /* What the host cannot do as the system does stops by name: a recursive mutex (released one
       level instead of all) and a condition attribute object (not established). */
    CHECK(ainit(&a) == 0 && settype(&a, 2) == 0 && init(&cond_recursive_mutex, &a, NULL) == 0);
    CHECK(adestroy(&a) == 0);
    CHECK(died_in_fatal(cond_wait_recursive));
    CHECK(died_in_fatal(cond_timedwait_recursive));
    CHECK(died_in_fatal(cond_init_with_attribute));
    CHECK(destroy(&cond_recursive_mutex) == 0);
    void *empty_attr = NULL, *default_cond = NULL;  /* an empty attribute variable: the defaults */
    CHECK(cond_init_entry(&default_cond, &empty_attr, NULL) == 0 && default_cond && cond_destroy(&default_cond) == 0);

    /* Destroyed, the variable holds 1 and every call on it is EINVAL. */
    CHECK(cond_destroy(&cond_shared) == 0 && cond_shared == (void *)1);
    CHECK(cond_destroy(&cond_shared) == (int)0x80020016);
    CHECK(cond_signal_entry(&cond_shared) == (int)0x80020016 && cond_broadcast(&cond_shared) == (int)0x80020016);
    CHECK(lock(&cond_mutex) == 0 && cond_wait_entry(&cond_shared, &cond_mutex) == (int)0x80020016);
    CHECK(cond_timedwait_entry(&cond_shared, &cond_mutex, 0) == (int)0x80020016 && unlock(&cond_mutex) == 0);
    CHECK(cond_init_entry(NULL, NULL, NULL) == (int)0x80020016 && cond_destroy(NULL) == (int)0x80020016);
    CHECK(cond_signal_entry(NULL) == (int)0x80020016 && cond_broadcast(NULL) == (int)0x80020016);

    /* The static initializer (NULL) becomes a cond on first use; destroyed unused, it stays NULL. */
    void *static_cond = NULL;
    CHECK(cond_destroy(&static_cond) == 0 && static_cond == NULL);
    CHECK(cond_signal_entry(&static_cond) == 0 && static_cond && static_cond != (void *)1);
    CHECK(cond_destroy(&static_cond) == 0 && static_cond == (void *)1);
    CHECK(destroy(&cond_mutex) == 0);
    alarm(0);
    CHECK(is_implemented("libkernel", "scePthreadCondInit") && is_implemented("libkernel", "scePthreadCondDestroy"));
    CHECK(is_implemented("libkernel", "scePthreadCondSignal") && is_implemented("libkernel", "scePthreadCondBroadcast"));
    CHECK(is_implemented("libkernel", "scePthreadCondWait") && is_implemented("libkernel", "scePthreadCondTimedwait"));

    /* the process parameter block comes from the loaded executable */
    static unsigned char block[0x40];
    rt_process.proc_param = block;
    CHECK(((void *(*)(void))fn("libkernel", "sceKernelGetProcParam"))() == block);

    /* a thread variable equals itself and nothing else: the same object pointer is the same thread.
       The runtime has a single guest thread so far (scePthreadCreate is not implemented), so the
       unequal cases compare it with a pointer that is not a thread: they exercise pointer inequality
       only, not two distinct valid thread handles. */
    static unsigned char main_stack[0x100];
    rt_thread_register_main(main_stack, sizeof main_stack);
    void *const self = ((void *(*)(void))fn("libkernel", "scePthreadSelf"))();
    const f_equal equal = (f_equal)fn("libkernel", "scePthreadEqual");
    unsigned char not_a_thread;
    CHECK(self && equal(self, self) != 0);
    CHECK(equal(self, &not_a_thread) == 0 && equal(&not_a_thread, self) == 0);
    CHECK(equal(self, NULL) == 0);

    /* A thread attribute takes one of the two detach states, joinable (0) or detached (1). Any
       other value, or a variable holding no attribute object, is EINVAL: an SCE code under the SCE
       name, the errno itself under the POSIX name. The state is read back the way thread creation
       will read it, through rt_thread_attr_detach_state. */
    const f1 thread_attr_init = (f1)fn("libkernel", "scePthreadAttrInit");
    const f1 thread_attr_destroy = (f1)fn("libkernel", "scePthreadAttrDestroy");
    const f_attr_get thread_attr_get = (f_attr_get)fn("libkernel", "scePthreadAttrGet");
    const f2i set_detach = (f2i)fn("libkernel", "scePthreadAttrSetdetachstate");
    const f2i posix_set_detach = (f2i)fn("libScePosix", "pthread_attr_setdetachstate");
    if (failures) return 1;
    void *thread_attr = NULL;
    CHECK(thread_attr_init(&thread_attr) == 0 && thread_attr);

    /* A fresh attribute has no state of its own, and a rejected value does not give it one:
       reading it stops instead of answering joinable. */
    CHECK(set_detach(&thread_attr, 2) == (int)0x80020016);
    CHECK(posix_set_detach(&thread_attr, -1) == 22);
    detach_reader_attr = thread_attr;
    CHECK(died_in_fatal(read_detach_state));

    /* The last state set is the one kept, and a rejected value leaves it alone. */
    CHECK(set_detach(&thread_attr, 1) == 0 && detach_state_of(&thread_attr) == 1);
    CHECK(set_detach(&thread_attr, 0) == 0 && detach_state_of(&thread_attr) == 0);
    CHECK(set_detach(&thread_attr, 1) == 0);
    CHECK(set_detach(&thread_attr, 2) == (int)0x80020016);
    CHECK(set_detach(&thread_attr, -1) == (int)0x80020016);
    CHECK(detach_state_of(&thread_attr) == 1);

    /* The POSIX name records on the same attribute object. */
    CHECK(posix_set_detach(&thread_attr, 0) == 0 && detach_state_of(&thread_attr) == 0);
    CHECK(posix_set_detach(&thread_attr, 2) == 22 && posix_set_detach(&thread_attr, -1) == 22);
    CHECK(detach_state_of(&thread_attr) == 0);
    CHECK(set_detach(NULL, 0) == (int)0x80020016 && posix_set_detach(NULL, 0) == 22);

    /* scePthreadAttrGet replaces the attribute with the running thread's, whose detach state the
       system supplies: the state set before is gone, and a state set afterwards is kept again. */
    CHECK(set_detach(&thread_attr, 1) == 0);
    CHECK(thread_attr_get(self, &thread_attr) == 0);
    detach_reader_attr = thread_attr;
    CHECK(died_in_fatal(read_detach_state));
    CHECK(set_detach(&thread_attr, 0) == 0 && detach_state_of(&thread_attr) == 0);

    CHECK(thread_attr_destroy(&thread_attr) == 0 && thread_attr == NULL);
    /* Destroyed, the variable holds no object: setting is EINVAL. In FreeBSD libthr, creating a
       thread from an empty variable, or from none, uses the default attributes, so reading stops
       for both. */
    CHECK(set_detach(&thread_attr, 0) == (int)0x80020016);
    CHECK(posix_set_detach(&thread_attr, 0) == 22);
    detach_reader_attr = thread_attr;
    CHECK(died_in_fatal(read_detach_state));
    CHECK(died_in_fatal(read_detach_state_without_variable));
    CHECK(is_implemented("libkernel", "scePthreadAttrSetdetachstate"));
    CHECK(is_implemented("libScePosix", "pthread_attr_setdetachstate"));

    /* The POSIX names of self, attribute init and destroy answer as the SCE names do, with the
       errno itself instead of an SCE code, and share the same attribute objects. */
    posix_self_entry = (void *(*)(void))fn("libScePosix", "pthread_self");
    const f1 posix_attr_init = (f1)fn("libScePosix", "pthread_attr_init");
    const f1 posix_attr_destroy = (f1)fn("libScePosix", "pthread_attr_destroy");
    if (failures) return 1;
    CHECK(posix_self_entry() == self);
    CHECK(died_in_fatal(posix_self_on_unregistered_thread));
    void *posix_attr = NULL;
    CHECK(posix_attr_init(&posix_attr) == 0 && posix_attr);
    CHECK(posix_set_detach(&posix_attr, 1) == 0 && detach_state_of(&posix_attr) == 1);
    CHECK(posix_attr_destroy(&posix_attr) == 0 && posix_attr == NULL);
    CHECK(posix_attr_destroy(&posix_attr) == 22 && posix_attr_destroy(NULL) == 22);
    CHECK(posix_attr_init(NULL) == 22);
    /* The SCE names keep their codes, and an object from one name is freed by the other. */
    CHECK(thread_attr_init(NULL) == (int)0x80020016 && thread_attr_destroy(&posix_attr) == (int)0x80020016);
    CHECK(posix_attr_init(&posix_attr) == 0 && thread_attr_destroy(&posix_attr) == 0 && posix_attr == NULL);
    CHECK(thread_attr_init(&posix_attr) == 0 && posix_attr_destroy(&posix_attr) == 0 && posix_attr == NULL);
    CHECK(is_implemented("libScePosix", "pthread_self"));
    CHECK(is_implemented("libScePosix", "pthread_attr_init") && is_implemented("libScePosix", "pthread_attr_destroy"));

    /* sched_yield gives up the processor and succeeds with 0, every time. That the host scheduler
       actually ran another thread is not observable from here, so only the contract's result is
       checked. */
    const f_check yield = (f_check)fn("libkernel", "sched_yield");
    CHECK(yield() == 0 && yield() == 0);
    CHECK(is_implemented("libkernel", "sched_yield"));

    /* scePthreadYield's PS4 signature (no result, or an int) is not established, so the entry stops
       by name and is registered without counting as implemented. */
    void (*pthread_yield_entry)(void) = (void (*)(void))fn("libkernel", "scePthreadYield");
    CHECK(died_in_fatal(pthread_yield_entry));
    CHECK(!is_implemented("libkernel", "scePthreadYield"));

    /* The PS4 host is little-endian AMD64; the network representation is big-endian. */
    CHECK(htonl(UINT32_C(0x12345678)) == UINT32_C(0x78563412));
    CHECK(htons(UINT16_C(0x1234)) == UINT16_C(0x3412));
    CHECK(ntohl(UINT32_C(0x78563412)) == UINT32_C(0x12345678));
    CHECK(ntohs(UINT16_C(0x3412)) == UINT16_C(0x1234));
    CHECK(htonl(0) == 0 && ntohl(0) == 0);
    CHECK(htons(UINT16_MAX) == UINT16_MAX && ntohs(UINT16_MAX) == UINT16_MAX);
    CHECK(ntohl(htonl(UINT32_C(0x01020304))) == UINT32_C(0x01020304));
    CHECK(ntohs(htons(UINT16_C(0x0102))) == UINT16_C(0x0102));

    /* The executable's observed use is AF_INET. Output is four network-order bytes; malformed
       text returns zero. Other address families and null-pointer behavior remain fail-loud. */
    unsigned char address[4] = {0};
    const f_inet_pton inet_pton_call = (f_inet_pton)net_inet_pton_entry;
    CHECK(is_implemented("libSceNet", "sceNetInetPton"));
    CHECK(inet_pton_call(2, "192.0.2.1", address) == 1);
    CHECK(address[0] == 192 && address[1] == 0 && address[2] == 2 && address[3] == 1);
    CHECK(inet_pton_call(2, "0.0.0.0", address) == 1);
    CHECK(address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0);
    CHECK(inet_pton_call(2, "255.255.255.255", address) == 1);
    CHECK(address[0] == 255 && address[1] == 255 && address[2] == 255 && address[3] == 255);
    CHECK(inet_pton_call(2, "192.0.2.256", address) == 0);
    CHECK(inet_pton_call(2, "192.0.2", address) == 0);
    CHECK(died_in_fatal(net_pton_unknown_family_call));
    CHECK(died_in_fatal(net_pton_null_source_call));
    CHECK(died_in_fatal(net_pton_null_destination_call));

    /* libSceNetCtl: the offline answers (issue #41). */
    netctl_get_state_entry = fn("libSceNetCtl", "sceNetCtlGetState");
    netctl_check_entry = fn("libSceNetCtl", "sceNetCtlCheckCallback");
    netctl_register_entry = fn("libSceNetCtl", "sceNetCtlRegisterCallback");
    netctl_unregister_entry = fn("libSceNetCtl", "sceNetCtlUnregisterCallback");
    netctl_get_info_entry = fn("libSceNetCtl", "sceNetCtlGetInfo");
    netctl_get_nat_info_entry = fn("libSceNetCtl", "sceNetCtlGetNatInfo");
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlGetState"));
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlCheckCallback"));
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlRegisterCallback"));
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlUnregisterCallback"));
    CHECK(!is_implemented("libSceNetCtl", "sceNetCtlGetInfo"));
    CHECK(!is_implemented("libSceNetCtl", "sceNetCtlGetNatInfo"));
    if (failures) return 1;
    netctl_shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (netctl_shared == MAP_FAILED) { perror("mmap"); return 1; }
    char poisoned[64];
    memset(poisoned, 0xa5, sizeof poisoned);
    memset(netctl_shared, 0xa5, sizeof poisoned);

    int state = -1;
    CHECK(netctl_state(&state) == 0 && state == 0);   /* the disconnected state */
    CHECK(netctl_check() == 0);                       /* nothing is due yet */

    /* A registered callback is kept, not run: offline there is no state change to report. The id
       comes back through the caller's pointer, which is what the guest's own sites read back. */
    CHECK(netctl_register((void *)netctl_must_not_run, &state, &netctl_id_a) == 0);
    CHECK(netctl_id_a != 0 && netctl_id_a != -1);       /* written, and never the unwritten look */
    CHECK(netctl_check() == 0);
    CHECK(netctl_callbacks_run == 0);

    /* Keyed by id rather than by state: a second registration is its own registration, and each
       removes under its own id. */
    CHECK(netctl_register((void *)netctl_must_not_run, NULL, &netctl_id_b) == 0);
    CHECK(netctl_id_b != netctl_id_a);
    CHECK(netctl_unregister(netctl_id_a) == 0);
    CHECK(netctl_check() == 0 && netctl_callbacks_run == 0);

    /* Loading a stub module marks it loaded and maps nothing; asking about it says loaded (0), and
       unloading clears the mark so it can be loaded again. Each answer the system's documentation
       does not give - a module the runtime cannot load, a second load, the "not loaded" error -
       stops by name. */
    sysmodule_load = (f_sysmodule)fn("libSceSysmodule", "sceSysmoduleLoadModule");
    sysmodule_is_loaded = (f_sysmodule)fn("libSceSysmodule", "sceSysmoduleIsLoaded");
    sysmodule_unload = (f_sysmodule)fn("libSceSysmodule", "sceSysmoduleUnloadModule");
    CHECK(is_implemented("libSceSysmodule", "sceSysmoduleLoadModule"));
    CHECK(is_implemented("libSceSysmodule", "sceSysmoduleIsLoaded"));
    CHECK(is_implemented("libSceSysmodule", "sceSysmoduleUnloadModule"));
    if (failures) return 1;
    rt_process.stub_modules[0] = (rt_stub_module){STUB_ID, "libSceTestStub", 0};
    rt_process.stub_modules[1] = (rt_stub_module){OTHER_STUB_ID, "libSceOtherTestStub", 0};
    rt_process.stub_module_count = 2;
    CHECK(died_in_fatal(load_not_a_stub));
    CHECK(died_in_fatal(is_loaded_not_a_stub));
    CHECK(died_in_fatal(unload_not_a_stub));
    CHECK(died_in_fatal(is_loaded_other_stub));       /* a stub not loaded yet */
    CHECK(died_in_fatal(unload_other_stub));
    CHECK(sysmodule_load(STUB_ID) == 0 && rt_process.stub_modules[0].loaded);
    CHECK(!rt_process.stub_modules[1].loaded);        /* only the module asked for */
    CHECK(sysmodule_is_loaded(STUB_ID) == 0);
    CHECK(died_in_fatal(load_stub_again));
    CHECK(sysmodule_unload(STUB_ID) == 0 && !rt_process.stub_modules[0].loaded);
    CHECK(sysmodule_load(STUB_ID) == 0 && sysmodule_is_loaded(STUB_ID) == 0);
    /* the id is 16 bits: the upper half of the register the guest passes it in is not part of it */
    CHECK(((int32_t (*)(uint32_t))(void *)sysmodule_unload)(UINT32_C(0xabcd0000) | STUB_ID) == 0);
    CHECK(!rt_process.stub_modules[0].loaded);

    /* Every answer the PS4 has not established stops the process instead of guessing, and the two
       structure calls stop before writing through a buffer whose layout is not established. */
    CHECK(died_in_fatal(unregister_id_a));             /* an id already removed */
    CHECK(died_in_fatal(unregister_unknown_id));       /* an id never handed back */
    CHECK(died_in_fatal(state_into_null));             /* no error value for a bad pointer */
    CHECK(died_in_fatal(register_null_callback));      /* no answer for a null callback */
    CHECK(died_in_fatal(register_null_id));            /* no answer for a null id slot */
    CHECK(died_in_fatal(info_of_module_one));
    CHECK(died_in_fatal(nat_info_with_size));
    CHECK(memcmp(netctl_shared, poisoned, sizeof poisoned) == 0);

    /* The store survived all of that, because every fatal above was in a child. */
    CHECK(netctl_unregister(netctl_id_b) == 0);
    CHECK(netctl_check() == 0 && netctl_callbacks_run == 0);

    /* the target page size has no public source, so both entries stop by name */
    void (*posix_getpagesize)(void) = (void (*)(void))fn("libScePosix", "getpagesize");
    void (*kernel_getpagesize)(void) = (void (*)(void))fn("libkernel", "getpagesize");
    CHECK(died_in_fatal(posix_getpagesize));
    CHECK(died_in_fatal(kernel_getpagesize));
    /* Registered so a boot that reaches the call names it, but not counted as implemented: the
       fatal alone does not prove the classification, since an absent entry would also never be
       reported as implemented. */
    CHECK(!is_implemented("libScePosix", "getpagesize"));
    CHECK(!is_implemented("libkernel", "getpagesize"));

    /* memcpy and memset (libSceLibcInternal): ISO C fixes the bytes written and the returned
       destination. The guard bytes past n prove nothing beyond n is touched. */
    void *(*libc_memcpy)(void *, const void *, size_t) =
        (void *(*)(void *, const void *, size_t))fn("libSceLibcInternal", "memcpy");
    void *(*libc_memset)(void *, int, size_t) = (void *(*)(void *, int, size_t))fn("libSceLibcInternal", "memset");
    const unsigned char source[5] = {1, 2, 3, 4, 5};
    unsigned char copy[6] = {0, 0, 0, 0, 0, 0xee};
    CHECK(libc_memcpy(copy, source, 5) == copy);
    CHECK(memcmp(copy, source, 5) == 0 && copy[5] == 0xee);
    CHECK(libc_memcpy(copy, (const unsigned char[1]){0x77}, 0) == copy && copy[0] == 1);
    unsigned char filled[4] = {0, 0, 0, 0xee};
    CHECK(libc_memset(filled, 0x1a5, 3) == filled);           /* c is converted to unsigned char */
    CHECK(filled[0] == 0xa5 && filled[1] == 0xa5 && filled[2] == 0xa5 && filled[3] == 0xee);
    CHECK(libc_memset(filled, 0, 0) == filled && filled[0] == 0xa5);
    CHECK(is_implemented("libSceLibcInternal", "memcpy") && is_implemented("libSceLibcInternal", "memset"));

    /* sceKernelUsleep and usleep sleep at least the microseconds asked and return 0. usleep's
       one-second-or-more range has no established PS4 answer, so it stops by name. */
    int (*kernel_usleep)(uint32_t) = (int (*)(uint32_t))fn("libkernel", "sceKernelUsleep");
    posix_usleep_entry = (int (*)(uint32_t))fn("libScePosix", "usleep");
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(kernel_usleep(2000) == 0 && elapsed_us(&start) >= 2000);
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(posix_usleep_entry(1500) == 0 && elapsed_us(&start) >= 1500);
    CHECK(kernel_usleep(0) == 0 && posix_usleep_entry(0) == 0);
    CHECK(died_in_fatal(usleep_one_second));
    CHECK(is_implemented("libkernel", "sceKernelUsleep") && is_implemented("libScePosix", "usleep"));

    /* __stack_chk_fail never returns: the program ends. That stop is the whole contract, so the
       entry counts as implemented, unlike the fail-loud diagnostics above. */
    void (*stack_chk_fail)(void) = (void (*)(void))fn("libkernel", "__stack_chk_fail");
    CHECK(died_in_fatal(stack_chk_fail));
    CHECK(is_implemented("libkernel", "__stack_chk_fail"));

    /* Direct memory, the way a title's startup heap uses it: the size, then a 2 MiB aligned range
       below it, mapped read/write where the system picks, released if the map fails. */
    const f_dm_size dm_size = (f_dm_size)fn("libkernel", "sceKernelGetDirectMemorySize");
    dm_allocate = (f_dm_allocate)fn("libkernel", "sceKernelAllocateDirectMemory");
    dm_map = (f_dm_map)fn("libkernel", "sceKernelMapDirectMemory");
    dm_release = (f_dm_release)fn("libkernel", "sceKernelReleaseDirectMemory");
    if (dm_size && dm_allocate && dm_map && dm_release) {
        const uint64_t pool = dm_size();
        CHECK(pool >= DM_LENGTH && pool % DM_PAGE == 0);
        int64_t first = -1, second = -1;
        CHECK(dm_allocate(0, (int64_t)pool, DM_LENGTH, DM_ALIGN, 0, &first) == 0);
        CHECK(first >= 0 && first % DM_ALIGN == 0 && (uint64_t)first + DM_LENGTH <= pool);
        CHECK(dm_allocate(0, (int64_t)pool, DM_LENGTH, DM_ALIGN, 0, &second) == 0);
        CHECK(second % DM_ALIGN == 0 && (second >= first + DM_LENGTH || second + DM_LENGTH <= first));
        void *a = NULL, *b = NULL;
        CHECK(dm_map(&a, DM_LENGTH, DM_RW, 0, first, DM_ALIGN) == 0 && a && (uintptr_t)a % DM_ALIGN == 0);
        CHECK(dm_map(&b, DM_LENGTH, DM_RW, 0, first, DM_ALIGN) == 0 && b && b != a);
        if (a && b) {
            /* Two maps of one range are the same memory, as physical memory is. */
            ((volatile unsigned char *)a)[DM_LENGTH - 1] = 0x5a;
            ((volatile unsigned char *)a)[0] = 0xa5;
            CHECK(((volatile unsigned char *)b)[DM_LENGTH - 1] == 0x5a && ((volatile unsigned char *)b)[0] == 0xa5);
        }
        /* A range never mapped can be released, and its space is found again. */
        CHECK(dm_release(second, DM_LENGTH) == 0);
        int64_t again = -1;
        CHECK(dm_allocate(0, (int64_t)pool, DM_LENGTH, DM_ALIGN, 0, &again) == 0 && again == second);
        /* The search range bounds where a range goes. */
        int64_t windowed = -1;
        CHECK(dm_allocate(1ll << 30, (1ll << 30) + DM_ALIGN, DM_PAGE, DM_PAGE, 0, &windowed) == 0 && windowed == 1ll << 30);
        dm_mapped = first;
        dm_unmapped = windowed;
        /* The answers that are not established stop by name. */
        CHECK(died_in_fatal(dm_allocate_too_much));
        CHECK(died_in_fatal(dm_allocate_no_room));
        CHECK(died_in_fatal(dm_allocate_odd_length));
        CHECK(died_in_fatal(dm_allocate_odd_alignment));
        CHECK(died_in_fatal(dm_allocate_unknown_type));
        CHECK(died_in_fatal(dm_allocate_no_variable));
        CHECK(died_in_fatal(dm_map_with_flags));
        CHECK(died_in_fatal(dm_map_with_hint));
        CHECK(died_in_fatal(dm_map_gpu));
        CHECK(died_in_fatal(dm_map_unallocated));
        CHECK(died_in_fatal(dm_map_past_range));
        CHECK(died_in_fatal(dm_release_mapped));
        CHECK(died_in_fatal(dm_release_part));
        CHECK(died_in_fatal(dm_release_unknown));
    }
    CHECK(is_implemented("libkernel", "sceKernelGetDirectMemorySize") &&
          is_implemented("libkernel", "sceKernelAllocateDirectMemory") &&
          is_implemented("libkernel", "sceKernelMapDirectMemory") &&
          is_implemented("libkernel", "sceKernelReleaseDirectMemory"));

    /* Kernel semaphores, as the executable uses them: created with attr 1 or 2 and no option, one
       unit waited for with no timeout, units signalled, deleted. */
    sema_create = (f_sema_create)fn("libkernel", "sceKernelCreateSema");
    sema_delete = (f_sema_one)fn("libkernel", "sceKernelDeleteSema");
    sema_wait = (f_sema_wait)fn("libkernel", "sceKernelWaitSema");
    sema_signal = (f_sema_count)fn("libkernel", "sceKernelSignalSema");
    sema_poll = (f_sema_count)fn("libkernel", "sceKernelPollSema");
    if (sema_create && sema_delete && sema_wait && sema_signal && sema_poll) {
        void *s = NULL;
        CHECK(sema_create(&s, "single", SEMA_PRIO, 1, 1, NULL) == 0 && s);
        CHECK(sema_wait(s, 1, NULL) == 0);
        CHECK(sema_signal(s, 1) == 0 && sema_poll(s, 1) == 0);
        CHECK(sema_signal(s, 1) == 0 && sema_delete(s) == 0);
        /* Two waiters on an empty FIFO semaphore are served in the order they arrived. One round
           would pass by chance half the time if the order were ignored, so it is repeated. */
        CHECK(sema_create(&sema_shared, "fifo", SEMA_FIFO, 0, 0xffff, NULL) == 0);
        for (int round = 0; round < 16; round++) {
            sema_served = 0;
            sema_order[0] = sema_order[1] = 0;
            pthread_t a, b;
            pthread_create(&a, NULL, sema_waiter, (void *)(intptr_t)1);
            while (sema_waiters(sema_shared) < 1) sched_yield();
            pthread_create(&b, NULL, sema_waiter, (void *)(intptr_t)2);
            while (sema_waiters(sema_shared) < 2) sched_yield();
            CHECK(sema_signal(sema_shared, 1) == 0);
            while (sema_served_now() < 1) sched_yield();
            CHECK(sema_order[0] == 1);
            CHECK(sema_signal(sema_shared, 1) == 0);
            pthread_join(a, NULL);
            pthread_join(b, NULL);
            CHECK(sema_served == 2 && sema_order[1] == 2);
        }
        CHECK(sema_create(&sema_prio, "prio", SEMA_PRIO, 0, 1, NULL) == 0);
        /* The answers that are not established stop by name. */
        CHECK(died_in_fatal(sema_create_no_variable));
        CHECK(died_in_fatal(sema_create_bad_attr));
        CHECK(died_in_fatal(sema_create_option));
        CHECK(died_in_fatal(sema_create_initial_over_max));
        CHECK(died_in_fatal(sema_wait_with_timeout));
        CHECK(died_in_fatal(sema_wait_null));
        CHECK(died_in_fatal(sema_signal_past_max));
        CHECK(died_in_fatal(sema_poll_empty));
        CHECK(died_in_fatal(sema_second_prio_waiter));
        CHECK(sema_delete(sema_shared) == 0);
    }
    CHECK(is_implemented("libkernel", "sceKernelCreateSema") && is_implemented("libkernel", "sceKernelDeleteSema") &&
          is_implemented("libkernel", "sceKernelWaitSema") && is_implemented("libkernel", "sceKernelSignalSema") &&
          is_implemented("libkernel", "sceKernelPollSema"));

    /* The time of day is the host's wall clock, under all three names. */
    tod_posix = (f_tod)fn("libScePosix", "gettimeofday");
    f_tod tod_kernel = (f_tod)fn("libkernel", "gettimeofday");
    tod_sce = (f_sce_tod)fn("libkernel", "sceKernelGettimeofday");
    if (tod_posix && tod_kernel && tod_sce) {
        struct timespec before, after;
        tod_timeval a = {-1, -1}, b = {-1, -1}, c = {-1, -1};
        clock_gettime(CLOCK_REALTIME, &before);
        CHECK(tod_posix(&a, NULL) == 0 && tod_kernel(&b, NULL) == 0 && tod_sce(&c) == 0);
        clock_gettime(CLOCK_REALTIME, &after);
        const tod_timeval *all[] = {&a, &b, &c};
        for (int i = 0; i < 3; i++) {
            const int64_t us = all[i]->seconds * 1000000 + all[i]->microseconds;
            CHECK(all[i]->microseconds >= 0 && all[i]->microseconds < 1000000);
            CHECK(us >= (int64_t)before.tv_sec * 1000000 + before.tv_nsec / 1000);
            CHECK(us <= (int64_t)after.tv_sec * 1000000 + after.tv_nsec / 1000);
        }
        CHECK(tod_posix(NULL, NULL) == 0); /* FreeBSD: no timeval, nothing filled, success */
        CHECK(died_in_fatal(tod_with_zone));
        CHECK(died_in_fatal(tod_sce_null));
    }
    CHECK(is_implemented("libScePosix", "gettimeofday") && is_implemented("libkernel", "gettimeofday") &&
          is_implemented("libkernel", "sceKernelGettimeofday"));

    if (!failures) puts("syslib: ok");
    return failures != 0;
}
