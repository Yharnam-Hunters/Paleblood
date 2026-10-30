/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The runtime's system library implementations, called the way the guest calls them: through the
   RT_SYSLIB registry, by library and symbol. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
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

/* Takes the recursive mutex only if the main thread's wait released every level it held. */
static f1 cond_trylock;
static int cond_recursive_go;
static void *cond_recursive_signaler(void *unused)
{
    (void)unused;
    for (int i = 0; i < 200000; i++) {
        if (cond_trylock(&cond_recursive_mutex) == 0) {
            cond_recursive_go = 1;
            cond_signal_entry(&cond_shared);
            cond_unlock(&cond_recursive_mutex);
            return NULL;
        }
        sched_yield();
    }
    return NULL;
}
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

/* Read-write locks. The write lock belongs to a thread, so some answers are checked from a second
   host thread: it needs no guest thread, only the rwlock object the SCE calls made. */
static finit rwlock_init;
static f1 rwlock_rdlock, rwlock_wrlock, rwlock_tryrdlock, rwlock_trywrlock, rwlock_unlock;
static void *rwlock_shared;
static pthread_mutex_t rwlock_done_guard = PTHREAD_MUTEX_INITIALIZER;
static int rwlock_reader_done; /* read and written under rwlock_done_guard */
static int reader_done(void)
{
    pthread_mutex_lock(&rwlock_done_guard);
    const int done = rwlock_reader_done;
    pthread_mutex_unlock(&rwlock_done_guard);
    return done;
}

typedef struct {
    f1 call;
    int result;
} rwlock_call;
static void *rwlock_call_run(void *arg)
{
    rwlock_call *c = arg;
    c->result = c->call(&rwlock_shared);
    return NULL;
}
/* The entry's answer for rwlock_shared, called on a new host thread. */
static int rwlock_from_other_thread(f1 call)
{
    rwlock_call c = {call, -1};
    pthread_t thread;
    if (pthread_create(&thread, NULL, rwlock_call_run, &c)) return -1;
    pthread_join(thread, NULL);
    return c.result;
}
static void *rwlock_blocked_reader(void *unused)
{
    (void)unused;
    const int r = rwlock_rdlock(&rwlock_shared);
    pthread_mutex_lock(&rwlock_done_guard);
    rwlock_reader_done = 1;
    pthread_mutex_unlock(&rwlock_done_guard);
    if (r == 0) rwlock_unlock(&rwlock_shared);
    return NULL;
}
static void rwlock_wrlock_again(void) { rwlock_wrlock(&rwlock_shared); }
static void rwlock_rdlock_while_writing(void) { rwlock_rdlock(&rwlock_shared); }
static void rwlock_init_with_attribute(void)
{
    void *lock = NULL, *attr = &lock; /* a variable holding some object */
    rwlock_init(&lock, &attr, "lock");
}

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
static void dm_map_exec(void) { void *a = NULL; dm_map(&a, DM_PAGE, DM_RW | 0x4, 0, dm_unmapped, DM_PAGE); }
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

/* Application content: the runtime answers only what the game's data declares (rt_process). */
typedef int32_t (*f_ac_init)(void *, void *);
typedef int32_t (*f_ac_param)(int32_t, int32_t *);
static f_ac_init ac_init;
static f_ac_param ac_param;
static void ac_undeclared_param(void) { int32_t v; ac_param(9, &v); }
static void ac_param_no_variable(void) { ac_param(1, NULL); }
static void ac_init_boot_param_not_declared(void)
{
    char init[32] = {0}, boot[40] = {0};
    rt_process.app_boot_param_unread = 0;
    ac_init(init, boot);
}
/* POSIX names of the condition variable calls: errno values, the same objects as the SCE names. */
typedef int (*f_pc_init)(void **, void **);
typedef int (*f_pc_one)(void **);
typedef int (*f_pc_wait)(void **, void **);
static f_pc_init pc_init;
static f_pc_one pc_signal;
static f_pc_wait pc_wait;
static void *pc_cond, *pc_mutex;
static int pc_go, pc_woken;
static int (*pc_lock)(void **), (*pc_unlock)(void **);
static void *pc_waiter(void *unused)
{
    (void)unused;
    pc_lock(&pc_mutex);
    int r = 0;
    while (!pc_go && !r) r = pc_wait(&pc_cond, &pc_mutex);
    if (!r) pc_woken = 1;
    pc_unlock(&pc_mutex);
    return NULL;
}
static void pc_init_with_attribute(void) { void *c = NULL, *attr = &c; pc_init(&c, &attr); }

/* Thread attribute setters: stack size and scheduling policy. */
typedef int (*f_attr_u64)(void **, uint64_t);
typedef int (*f_attr_int)(void **, int);
static f_attr_u64 ta_stack;
static f_attr_int ta_policy;
static void ta_policy_unknown(void) { void *a = &a; ta_policy(&a, 4); }

/* Thread creation: the entry is host code here, called with the guest's convention. */
typedef int (*f_create)(void **, void **, void *(*)(void *), void *, const char *);
typedef int (*f_setaffinity)(void *, uint64_t);
typedef void *(*f_self)(void);
static f_create tc_create;
static f_setaffinity tc_affinity;
static f_self tc_self;
static void *tc_seen_self, *tc_seen_argument;
static uint64_t tc_seen_tp, tc_seen_tp_word;
static uintptr_t tc_seen_local;
static atomic_int tc_done;
/* _sigprocmask: each guest thread's blocked signals, a FreeBSD sigset_t of four words. */
typedef int (*f_sigmask)(int, const uint32_t *, uint32_t *);
static f_sigmask tc_sigmask;
static uint32_t tc_seen_mask[4];
static void sigmask_bad_how(void) { const uint32_t set[4] = {0}; tc_sigmask(4, set, NULL); }
/* Thread priorities: the main thread starts at 700 unless the executable declares otherwise. */
typedef int (*f_getprio)(void *, int32_t *);
typedef int (*f_setprio)(void *, int32_t);
static f_getprio tc_getprio;
static f_setprio tc_setprio;
static int32_t tc_seen_priority;
static void setprio_out_of_range(void) { tc_setprio(tc_self(), 0xff); }
typedef int (*f_attr_schedparam)(void **, const int32_t *);
static f_attr_schedparam tc_attr_schedparam;
static void main_thread_declared(void)
{
    static uint64_t param[8] = {0x40, 0, 0, 0, 0, 0x1234, 0, 0}; /* a slot in +0x18..+0x37 set */
    rt_process.proc_param = param;
    static unsigned char stack[0x100];
    rt_thread_register_main(stack, sizeof stack);
}
static void *tc_entry(void *argument)
{
    int local = 0;
    tc_seen_argument = argument;
    tc_seen_self = tc_self();
    if (tc_sigmask) tc_sigmask(1, NULL, tc_seen_mask);
    if (tc_getprio) tc_getprio(tc_self(), &tc_seen_priority);
    uint64_t tp, word;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(word));
    tp = word; /* the thread pointer's first word is the thread pointer itself */
    tc_seen_tp = tp;
    tc_seen_tp_word = *(uint64_t *)(uintptr_t)tp;
    tc_seen_local = (uintptr_t)&local;
    atomic_store(&tc_done, 1);
    return NULL;
}
static void *tc_attr(int with_stack, int with_detach, int detach)
{
    void *a = NULL;
    ((f1)fn("libkernel", "scePthreadAttrInit"))(&a);
    if (with_stack) ((int (*)(void **, uint64_t))fn("libkernel", "scePthreadAttrSetstacksize"))(&a, 0x10000);
    if (with_detach) ((f2i)fn("libkernel", "scePthreadAttrSetdetachstate"))(&a, detach);
    return a;
}
typedef int (*f_attr_affinity)(void **, uint64_t);
static f_attr_affinity tc_attr_affinity;
static void attr_affinity_empty(void) { void *a = tc_attr(1, 1, 0); tc_attr_affinity(&a, 0); }
static void attr_schedparam_out_of_range(void) { void *a = tc_attr(1, 1, 0); const int32_t p = 0x300; tc_attr_schedparam(&a, &p); }
static void tc_create_no_attr(void) { void *t; tc_create(&t, NULL, tc_entry, NULL, "t"); }
static void tc_create_no_stack(void) { void *t, *a = tc_attr(0, 1, 0); tc_create(&t, &a, tc_entry, NULL, "t"); }
static void tc_create_no_detach(void) { void *t, *a = tc_attr(1, 0, 0); tc_create(&t, &a, tc_entry, NULL, "t"); }
static void tc_affinity_empty(void) { tc_affinity(tc_seen_self, 0); }

/* The user service's start-up calls. */
typedef int32_t (*f_us_init)(const uint32_t *);
typedef int32_t (*f_us_none)(void);
typedef int32_t (*f_us_ids)(int32_t *);
static f_us_none us_terminate;
static f_us_ids us_initial_user, us_login_list;
static void us_terminate_uninitialized(void) { us_terminate(); }
static void us_initial_user_uninitialized(void) { int32_t u; us_initial_user(&u); }

/* System settings: answered from rt_process.system_params. */
typedef int32_t (*f_sys_param)(int32_t, int32_t *);
static f_sys_param sys_param;
static void sys_param_unset(void) { int32_t v; sys_param(4, &v); }
static void sys_param_no_variable(void) { sys_param(1, NULL); }

/* libSceNpManager: content restriction, offline. */
typedef int32_t (*f_np_restrict)(const void *);
static f_np_restrict np_restrict;
static void np_restrict_null(void) { np_restrict(NULL); }

/* Flexible memory: fresh host memory at an address the runtime picks. */
typedef int32_t (*f_flex_map)(void **, uint64_t, int32_t, int32_t);
static f_flex_map flex_map;
static void flex_no_variable(void) { flex_map(NULL, 0x4000, 3, 0); }
static void flex_hint(void) { void *a = (void *)0x10000000; flex_map(&a, 0x4000, 3, 0); }
static void flex_flags(void) { void *a = NULL; flex_map(&a, 0x4000, 3, 0x10); }
static void flex_partial_page(void) { void *a = NULL; flex_map(&a, 0x1000, 3, 0); }
typedef int32_t (*f_qmp_fn)(const void *, void **, void **, int32_t *);
static f_qmp_fn qmp_fn;
static void qmp_unmapped(void) { int32_t p; static char not_mapped; qmp_fn(&not_mapped, NULL, NULL, &p); }
static void qmp_with_end(void) { int32_t p; void *end; static char x; qmp_fn(&x, NULL, &end, &p); }
static void flex_exec(void) { void *a = NULL; flex_map(&a, 0x4000, 7, 0); }

/* libSceNet start-up: initialize once, a repeat answers 0x80410110; pools are ids. */
typedef int32_t (*f_net_none)(void);
typedef int32_t (*f_net_pool)(const char *, int32_t, int32_t);
static f_net_none net_term_fn;
static f_net_pool net_pool_fn;
static void net_term_uninitialized(void) { net_term_fn(); }
static void net_pool_uninitialized(void) { net_pool_fn("pool", 0x1000, 0); }
static void net_pool_flags(void) { net_pool_fn("pool", 0x1000, 1); }
static void net_pool_no_name(void) { net_pool_fn(NULL, 0x1000, 0); }

/* libSceSsl / libSceHttp start-up: context ids. */
typedef int32_t (*f_ssl_init)(uint64_t);
typedef int32_t (*f_http_init)(int32_t, int32_t, uint64_t);
static f_http_init http_init_fn;
static int32_t http_test_pool, http_test_ssl;
static void http_init_unknown_pool(void) { http_init_fn(999, http_test_ssl, 0x4000); }
static void http_init_unknown_ssl(void) { http_init_fn(http_test_pool, 999, 0x4000); }
typedef int32_t (*f_http_template)(int32_t, const char *, int32_t, int32_t);
static f_http_template http_template_fn;
static int32_t http_test_context;
static void http_template_unknown_context(void) { http_template_fn(999, "agent", 2, 1); }
static void http_template_no_agent(void) { http_template_fn(http_test_context, NULL, 2, 1); }
static void http_template_version(void) { http_template_fn(http_test_context, "agent", 3, 1); }
static void http_template_proxy(void) { http_template_fn(http_test_context, "agent", 2, 0); }
typedef int32_t (*f_http_nonblock)(int32_t, int32_t);
static f_http_nonblock http_nonblock_fn;
static void http_nonblock_unknown(void) { http_nonblock_fn(999, 1); }
static void http_nonblock_value(void) { http_nonblock_fn(1, 2); }
typedef int32_t (*f_http_epoll)(int32_t, void **);
static f_http_epoll http_epoll_fn;
static void http_epoll_unknown(void) { void *h = NULL; http_epoll_fn(999, &h); }
static void http_epoll_no_slot(void) { http_epoll_fn(http_test_context, NULL); }
typedef int32_t (*f_http_header)(int32_t, const char *, const char *, int32_t);
static f_http_header http_header_fn;
static void http_header_unknown(void) { http_header_fn(999, "Accept", "*/*", 0); }
static void http_header_no_value(void) { http_header_fn(1, "Accept", NULL, 0); }
static void http_header_mode(void) { http_header_fn(1, "Accept", "*/*", 1); }

/* clock_gettime with FreeBSD's clock numbers. */
typedef int (*f_clock_gettime)(int32_t, int64_t *);
static f_clock_gettime clock_gettime_fn;
static void clock_unknown(void) { int64_t ts[2]; clock_gettime_fn(1, ts); }
static void clock_no_timespec(void) { clock_gettime_fn(0, NULL); }
typedef uint64_t (*f_u64)(void);

/* libSceAjm start-up: a context id, then the observed module 1 registered once. */
typedef int32_t (*f_ajm_init)(int64_t, uint32_t *);
typedef int32_t (*f_ajm_register)(uint32_t, uint32_t, uint64_t);
static f_ajm_init ajm_init_fn;
static f_ajm_register ajm_register_fn;
static uint32_t ajm_test_context;
static void ajm_init_reserved(void) { uint32_t c; ajm_init_fn(1, &c); }
static void ajm_register_unknown_context(void) { ajm_register_fn(99, 1, 0); }
static void ajm_register_other_module(void) { ajm_register_fn(ajm_test_context, 2, 0); }
static void ajm_register_twice(void) { ajm_register_fn(ajm_test_context, 1, 0); }

/* libSceAudioOut start-up: initialize once, then open the observed main port. */
typedef int32_t (*f_ao_init)(void);
typedef int32_t (*f_ao_open)(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
static f_ao_open ao_open_fn;
static void ao_open_s16(void) { ao_open_fn(0xff, 0, 0, 256, 48000, 1); }
static void ao_open_other_rate(void) { ao_open_fn(0xff, 0, 0, 256, 44100, 4); }
static void ao_open_other_user(void) { ao_open_fn(1, 0, 0, 256, 48000, 4); }
static void ao_open_port_type_2(void) { ao_open_fn(0xff, 2, 0, 256, 48000, 4); }
typedef int32_t (*f_ao_volume)(int32_t, int32_t, const int32_t *);
static f_ao_volume ao_volume_fn;
static int32_t ao_stereo_port;
static const int32_t ao_unity[8] = {32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768};
static void ao_volume_extra_channel(void) { ao_volume_fn(ao_stereo_port, 0x7, ao_unity); }
static void ao_volume_unknown_port(void) { ao_volume_fn(99, 0x3, ao_unity); }
typedef int32_t (*f_ao_output)(int32_t, const void *);
static f_ao_output ao_output_fn;
static void ao_output_no_samples(void) { ao_output_fn(1, NULL); }
static void ao_volume_too_loud(void) { const int32_t v[8] = {40000}; ao_volume_fn(ao_stereo_port, 0x1, v); }

/* sceKernelStat under /app0, served from a scratch directory. */
typedef int32_t (*f_kstat)(const char *, void *);
static f_kstat kstat_fn;
static void kstat_outside(void) { unsigned char st[120]; kstat_fn("/download0/x", st); }
static void kstat_dotdot(void) { unsigned char st[120]; kstat_fn("/app0/../etc/passwd", st); }
static void kstat_prefix_only(void) { unsigned char st[120]; kstat_fn("/app0x/file", st); }

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

    /* A recursive mutex held twice: the wait releases both levels (another thread can take the
       mutex), and when it returns the caller holds both again (libthr _mutex_cv_unlock/_lock). */
    CHECK(ainit(&a) == 0 && settype(&a, 2) == 0 && init(&cond_recursive_mutex, &a, NULL) == 0);
    CHECK(adestroy(&a) == 0);
    CHECK(cond_wait_entry(&cond_shared, &cond_recursive_mutex) == (int)0x80020001); /* not owned */
    cond_trylock = trylock;
    CHECK(lock(&cond_recursive_mutex) == 0 && lock(&cond_recursive_mutex) == 0);
    for (int round = 0; round < 2; round++) { /* the second round needs the depth the first restored */
        cond_recursive_go = 0;
        pthread_t recursive_signaler;
        if (pthread_create(&recursive_signaler, NULL, cond_recursive_signaler, NULL)) { perror("pthread_create"); return 1; }
        for (int i = 0; i < 50 && !cond_recursive_go; i++)
            cond_timedwait_entry(&cond_shared, &cond_recursive_mutex, 100000);
        pthread_join(recursive_signaler, NULL);
        CHECK(cond_recursive_go == 1);
    }
    CHECK(unlock(&cond_recursive_mutex) == 0 && unlock(&cond_recursive_mutex) == 0);
    CHECK(unlock(&cond_recursive_mutex) == (int)0x80020001); /* both levels given back, no more */
    /* A condition attribute object is not established and stops by name. */
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
    /* The executable keeps a thread variable's value in 40 bits (its lock at 0x024368c0). */
    CHECK((uintptr_t)self < (1ull << 40));
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

    /* Read-write locks (SCE names): SCE codes for FreeBSD libthr's and the umtx lock's errors. */
    rwlock_init = (finit)fn("libkernel", "scePthreadRwlockInit");
    const f1 rwlock_destroy = (f1)fn("libkernel", "scePthreadRwlockDestroy");
    rwlock_rdlock = (f1)fn("libkernel", "scePthreadRwlockRdlock");
    rwlock_wrlock = (f1)fn("libkernel", "scePthreadRwlockWrlock");
    rwlock_tryrdlock = (f1)fn("libkernel", "scePthreadRwlockTryrdlock");
    rwlock_trywrlock = (f1)fn("libkernel", "scePthreadRwlockTrywrlock");
    rwlock_unlock = (f1)fn("libkernel", "scePthreadRwlockUnlock");
    if (failures) return 1;
    alarm(60); /* a lock that never comes kills the test with SIGALRM instead of hanging it */
    CHECK(rwlock_init(&rwlock_shared, NULL, "rwlock") == 0 && rwlock_shared && rwlock_shared != (void *)1);

    /* Readers share the lock, the same thread too, and keep a writer out; each read lock is
       unlocked once, and an unlock with nothing held is EPERM. */
    CHECK(rwlock_rdlock(&rwlock_shared) == 0 && rwlock_rdlock(&rwlock_shared) == 0);
    CHECK(rwlock_tryrdlock(&rwlock_shared) == 0);
    CHECK(rwlock_from_other_thread(rwlock_tryrdlock) == 0);  /* a reader on another thread */
    CHECK(rwlock_trywrlock(&rwlock_shared) == (int)0x80020010);
    CHECK(rwlock_from_other_thread(rwlock_trywrlock) == (int)0x80020010);
    for (int i = 0; i < 4; i++) CHECK(rwlock_unlock(&rwlock_shared) == 0);
    CHECK(rwlock_unlock(&rwlock_shared) == (int)0x80020001);

    /* The writer keeps everyone else out, and only the writer unlocks it. */
    CHECK(rwlock_wrlock(&rwlock_shared) == 0);
    CHECK(rwlock_tryrdlock(&rwlock_shared) == (int)0x80020010 && rwlock_trywrlock(&rwlock_shared) == (int)0x80020010);
    CHECK(rwlock_from_other_thread(rwlock_tryrdlock) == (int)0x80020010);
    CHECK(rwlock_from_other_thread(rwlock_unlock) == (int)0x80020001);
    /* Locking again while holding the write lock would wait forever on the system: stops by name. */
    CHECK(died_in_fatal(rwlock_wrlock_again));
    CHECK(died_in_fatal(rwlock_rdlock_while_writing));
    /* A reader on another thread waits until the writer unlocks. */
    pthread_t reader;
    if (pthread_create(&reader, NULL, rwlock_blocked_reader, NULL)) { perror("pthread_create"); return 1; }
    usleep(20000);
    CHECK(reader_done() == 0);
    CHECK(rwlock_unlock(&rwlock_shared) == 0);
    pthread_join(reader, NULL);
    CHECK(reader_done() == 1);
    CHECK(rwlock_unlock(&rwlock_shared) == (int)0x80020001);

    /* An attribute object is not established; an empty attribute variable means the defaults. */
    CHECK(died_in_fatal(rwlock_init_with_attribute));
    void *empty_rwlock_attr = NULL, *default_rwlock = NULL;
    CHECK(rwlock_init(&default_rwlock, &empty_rwlock_attr, NULL) == 0 && default_rwlock && rwlock_destroy(&default_rwlock) == 0);

    /* Destroying does not check whether the lock is held (libthr). Destroyed, the variable holds 1
       and every call on it is EINVAL; so is a missing variable. */
    CHECK(rwlock_rdlock(&rwlock_shared) == 0);
    CHECK(rwlock_destroy(&rwlock_shared) == 0 && rwlock_shared == (void *)1);
    CHECK(rwlock_destroy(&rwlock_shared) == (int)0x80020016);
    CHECK(rwlock_rdlock(&rwlock_shared) == (int)0x80020016 && rwlock_wrlock(&rwlock_shared) == (int)0x80020016);
    CHECK(rwlock_tryrdlock(&rwlock_shared) == (int)0x80020016 && rwlock_trywrlock(&rwlock_shared) == (int)0x80020016);
    CHECK(rwlock_unlock(&rwlock_shared) == (int)0x80020016);
    CHECK(rwlock_init(NULL, NULL, NULL) == (int)0x80020016 && rwlock_destroy(NULL) == (int)0x80020016);
    CHECK(rwlock_rdlock(NULL) == (int)0x80020016 && rwlock_unlock(NULL) == (int)0x80020016);

    /* The static initializer (NULL) becomes a rwlock on first lock; unlocked before that, it is
       EPERM, and destroyed unused it stays NULL. */
    void *static_rwlock = NULL;
    CHECK(rwlock_unlock(&static_rwlock) == (int)0x80020001 && static_rwlock == NULL);
    CHECK(rwlock_destroy(&static_rwlock) == 0 && static_rwlock == NULL);
    CHECK(rwlock_wrlock(&static_rwlock) == 0 && static_rwlock && static_rwlock != (void *)1);
    CHECK(rwlock_unlock(&static_rwlock) == 0 && rwlock_destroy(&static_rwlock) == 0 && static_rwlock == (void *)1);
    alarm(0);
    CHECK(is_implemented("libkernel", "scePthreadRwlockInit") && is_implemented("libkernel", "scePthreadRwlockDestroy"));
    CHECK(is_implemented("libkernel", "scePthreadRwlockRdlock") && is_implemented("libkernel", "scePthreadRwlockWrlock"));
    CHECK(is_implemented("libkernel", "scePthreadRwlockTryrdlock") && is_implemented("libkernel", "scePthreadRwlockTrywrlock"));
    CHECK(is_implemented("libkernel", "scePthreadRwlockUnlock"));

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
        CHECK(died_in_fatal(dm_map_exec));
        /* GPU access (0x10 read, 0x20 write) is accepted; the CPU bits make the host mapping. */
        void *gpu = NULL;
        CHECK(dm_map(&gpu, DM_PAGE, DM_RW | 0x30, 0, dm_unmapped, DM_PAGE) == 0 && gpu);
        if (gpu) ((volatile unsigned char *)gpu)[0] = 1;
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

    /* libSceAppContent: declared values are written, unread ones succeed without writing, the
       boot parameter is passed over only when declared unread, everything else stops by name. */
    ac_init = (f_ac_init)fn("libSceAppContent", "sceAppContentInitialize");
    ac_param = (f_ac_param)fn("libSceAppContent", "sceAppContentAppParamGetInt");
    if (ac_init && ac_param) {
        rt_process.app_params[0] = (rt_app_param){1, 0, 13};
        rt_process.app_params[1] = (rt_app_param){0, 1, 0};
        rt_process.app_param_count = 2;
        rt_process.app_boot_param_unread = 1;
        char init[32] = {0}, boot[40];
        memset(boot, 0x5a, sizeof boot);
        CHECK(ac_init(init, boot) == 0);
        CHECK(boot[0] == 0x5a && boot[sizeof boot - 1] == 0x5a); /* not written */
        int32_t value = -7;
        CHECK(ac_param(1, &value) == 0 && value == 13);
        value = -7;
        CHECK(ac_param(0, &value) == 0 && value == -7);
        CHECK(died_in_fatal(ac_undeclared_param));
        CHECK(died_in_fatal(ac_param_no_variable));
        CHECK(died_in_fatal(ac_init_boot_param_not_declared));
        rt_process.app_param_count = 0;
        rt_process.app_boot_param_unread = 0;
    }
    CHECK(is_implemented("libSceAppContent", "sceAppContentInitialize") &&
          is_implemented("libSceAppContent", "sceAppContentAppParamGetInt"));
    /* libScePosix pthread_cond_*: errno values; pthread_cond_timedwait stays unbound. */
    pc_init = (f_pc_init)fn("libScePosix", "pthread_cond_init");
    const f_pc_one pc_destroy = (f_pc_one)fn("libScePosix", "pthread_cond_destroy");
    pc_signal = (f_pc_one)fn("libScePosix", "pthread_cond_signal");
    const f_pc_one pc_broadcast = (f_pc_one)fn("libScePosix", "pthread_cond_broadcast");
    pc_wait = (f_pc_wait)fn("libScePosix", "pthread_cond_wait");
    const f_pc_init pm_init = (f_pc_init)fn("libScePosix", "pthread_mutex_init");
    pc_lock = (int (*)(void **))fn("libScePosix", "pthread_mutex_lock");
    pc_unlock = (int (*)(void **))fn("libScePosix", "pthread_mutex_unlock");
    const f1 sce_cond_destroy = (f1)fn("libkernel", "scePthreadCondDestroy");
    if (pc_init && pc_destroy && pc_signal && pc_broadcast && pc_wait && pm_init && pc_lock && pc_unlock && sce_cond_destroy) {
        void *c = NULL;
        CHECK(pc_init(&c, NULL) == 0 && c);
        CHECK(pc_broadcast(&c) == 0 && pc_signal(&c) == 0);
        CHECK(pc_destroy(&c) == 0);
        CHECK(pc_destroy(&c) == 22);                  /* EINVAL itself under the POSIX name */
        CHECK(sce_cond_destroy(&c) == (int)0x80020016u); /* and as an SCE code under the SCE name */
        CHECK(pc_init(&pc_cond, NULL) == 0 && pm_init(&pc_mutex, NULL) == 0);
        pthread_t t;
        pthread_create(&t, NULL, pc_waiter, NULL);
        for (int i = 0; i < 200; i++) sched_yield();
        CHECK(pc_lock(&pc_mutex) == 0);
        pc_go = 1;
        CHECK(pc_signal(&pc_cond) == 0);
        CHECK(pc_unlock(&pc_mutex) == 0);
        pthread_join(t, NULL);
        CHECK(pc_woken == 1);
        CHECK(died_in_fatal(pc_init_with_attribute));
    }
    CHECK(is_implemented("libScePosix", "pthread_cond_init") && is_implemented("libScePosix", "pthread_cond_wait") &&
          is_implemented("libScePosix", "pthread_cond_signal") && is_implemented("libScePosix", "pthread_cond_broadcast") &&
          is_implemented("libScePosix", "pthread_cond_destroy") && !is_implemented("libScePosix", "pthread_cond_timedwait"));

    /* scePthreadAttrSetstacksize / Setschedpolicy and pthread_attr_setstacksize, on attributes
       made by scePthreadAttrInit. */
    ta_stack = (f_attr_u64)fn("libkernel", "scePthreadAttrSetstacksize");
    ta_policy = (f_attr_int)fn("libkernel", "scePthreadAttrSetschedpolicy");
    const f_attr_u64 ta_posix_stack = (f_attr_u64)fn("libScePosix", "pthread_attr_setstacksize");
    const f1 ta_init = (f1)fn("libkernel", "scePthreadAttrInit"), ta_destroy = (f1)fn("libkernel", "scePthreadAttrDestroy");
    if (ta_stack && ta_policy && ta_posix_stack && ta_init && ta_destroy) {
        void *a = NULL;
        CHECK(ta_init(&a) == 0);
        CHECK(ta_stack(&a, 0x10000) == 0 && ta_stack(&a, 0x4000) == 0 && ta_posix_stack(&a, 0x20000) == 0);
        CHECK(ta_policy(&a, 1) == 0 && ta_policy(&a, 2) == 0 && ta_policy(&a, 3) == 0);
        void *none = NULL;
        CHECK(ta_stack(&none, 0x10000) == (int)0x80020016u && ta_posix_stack(&none, 0x10000) == 22);
        CHECK(ta_policy(&none, 1) == (int)0x80020016u);
        /* Below 16 KiB is refused, as the executable expects: it then asks for exactly 16 KiB. */
        CHECK(ta_stack(&a, 0x3fff) == (int)0x80020016u && ta_posix_stack(&a, 0x1000) == 22);
        CHECK(died_in_fatal(ta_policy_unknown));
        CHECK(ta_destroy(&a) == 0);
    }

    /* scePthreadCreate: the guest runs on a new host thread, on its own stack, with GS pointing at
       a fresh TLS block, registered for scePthreadSelf. */
    tc_create = (f_create)fn("libkernel", "scePthreadCreate");
    tc_affinity = (f_setaffinity)fn("libkernel", "scePthreadSetaffinity");
    tc_self = (f_self)fn("libkernel", "scePthreadSelf");
    tc_sigmask = (f_sigmask)fn("libkernel", "_sigprocmask");
    if (tc_sigmask && tc_self) {
        /* Block, read back, unblock; SIGKILL (bit 8) and SIGSTOP (bit 16) are never blocked. */
        uint32_t old[4] = {7, 7, 7, 7};
        CHECK(tc_sigmask(1, NULL, old) == 0 && !old[0] && !old[1] && !old[2] && !old[3]);
        const uint32_t block[4] = {0x80 | 0x100 | 0x10000, 0, 0, 0x2};
        CHECK(tc_sigmask(1, block, NULL) == 0);
        CHECK(tc_sigmask(1, NULL, old) == 0 && old[0] == 0x80 && old[3] == 0x2);
        const uint32_t unblock[4] = {0x80, 0, 0, 0};
        CHECK(tc_sigmask(2, unblock, old) == 0 && old[0] == 0x80);
        CHECK(tc_sigmask(1, NULL, old) == 0 && old[0] == 0 && old[3] == 0x2);
        const uint32_t set_to[4] = {0x4, 0, 0, 0};
        CHECK(tc_sigmask(3, set_to, NULL) == 0);
        CHECK(tc_sigmask(1, NULL, old) == 0 && old[0] == 0x4 && old[3] == 0);
        CHECK(tc_sigmask(99, NULL, NULL) == 0); /* how is ignored without a new set */
        CHECK(died_in_fatal(sigmask_bad_how));
    }
    tc_getprio = (f_getprio)fn("libkernel", "scePthreadGetprio");
    tc_setprio = (f_setprio)fn("libkernel", "scePthreadSetprio");
    if (tc_getprio && tc_setprio && tc_self) {
        int32_t priority = 0;
        CHECK(tc_getprio(tc_self(), &priority) == 0 && priority == 700);
        CHECK(tc_setprio(tc_self(), 0x2ff) == 0 && tc_getprio(tc_self(), &priority) == 0 && priority == 0x2ff);
        CHECK(tc_setprio(tc_self(), 0x100) == 0 && tc_getprio(tc_self(), &priority) == 0 && priority == 0x100);
        CHECK(died_in_fatal(setprio_out_of_range));
        CHECK(died_in_fatal(main_thread_declared));
        CHECK(tc_setprio(tc_self(), 0x123) == 0); /* the worker below inherits it */
    }
    tc_attr_schedparam = (f_attr_schedparam)fn("libkernel", "scePthreadAttrSetschedparam");
    if (tc_attr_schedparam && tc_create && tc_self) {
        void *empty = NULL;
        const int32_t p = 0x2ff;
        CHECK(tc_attr_schedparam(&empty, &p) == (int)0x80020016);
        CHECK(died_in_fatal(attr_schedparam_out_of_range));
    }
    tc_attr_affinity = (f_attr_affinity)fn("libkernel", "scePthreadAttrSetaffinity");
    if (tc_attr_affinity && tc_create) {
        void *empty = NULL, *a = tc_attr(1, 1, 0);
        CHECK(tc_attr_affinity(&empty, 0x3f) == (int)0x80020016);
        CHECK(tc_attr_affinity(&a, 0x3f) == 0);
        CHECK(died_in_fatal(attr_affinity_empty));
    }
    if (tc_create && tc_affinity && tc_self) {
        void *thread = NULL, *attr = tc_attr(1, 1, 0);
        int marker = 0;
        if (tc_attr_schedparam) {
            const int32_t explicit_priority = 0x2ff; /* ignored: the attribute inherits (libthr default) */
            CHECK(tc_attr_schedparam(&attr, &explicit_priority) == 0);
        }
        CHECK(tc_create(&thread, &attr, tc_entry, &marker, "worker") == 0 && thread);
        CHECK((uintptr_t)thread < (1ull << 40));
        for (int i = 0; i < 100000 && !atomic_load(&tc_done); i++) sched_yield();
        CHECK(atomic_load(&tc_done) == 1);
        CHECK(tc_seen_argument == &marker && tc_seen_self == thread && tc_seen_self != tc_self());
        CHECK(tc_seen_tp && tc_seen_tp_word == tc_seen_tp);
        CHECK(!tc_sigmask || (tc_seen_mask[0] == 0x4 && tc_seen_mask[3] == 0)); /* inherited */
        CHECK(!tc_getprio || tc_seen_priority == 0x123); /* inherited */
        CHECK(tc_seen_local < (uintptr_t)&marker - 0x100000 || tc_seen_local > (uintptr_t)&marker + 0x100000);
        CHECK(tc_affinity(thread, 0x3f) == 0);
        atomic_store(&tc_done, 0);
        void *detached = NULL, *dattr = tc_attr(1, 1, 1);
        CHECK(tc_create(&detached, &dattr, tc_entry, NULL, NULL) == 0 && detached);
        for (int i = 0; i < 100000 && !atomic_load(&tc_done); i++) sched_yield();
        CHECK(atomic_load(&tc_done) == 1 && tc_seen_self == detached);
        CHECK(died_in_fatal(tc_create_no_attr));
        CHECK(died_in_fatal(tc_create_no_stack));
        CHECK(died_in_fatal(tc_create_no_detach));
        CHECK(died_in_fatal(tc_affinity_empty));
    }

    /* libSceUserService: one user, logged in from the start; a second initialize answers
       0x80960003, as the executable's second site expects. */
    const f_us_init us_init = (f_us_init)fn("libSceUserService", "sceUserServiceInitialize");
    us_terminate = (f_us_none)fn("libSceUserService", "sceUserServiceTerminate");
    us_initial_user = (f_us_ids)fn("libSceUserService", "sceUserServiceGetInitialUser");
    us_login_list = (f_us_ids)fn("libSceUserService", "sceUserServiceGetLoginUserIdList");
    if (us_init && us_terminate && us_initial_user && us_login_list) {
        CHECK(died_in_fatal(us_terminate_uninitialized));
        CHECK(died_in_fatal(us_initial_user_uninitialized));
        const uint32_t params[4] = {0x2bc, 0, 0, 0};
        CHECK(us_init(params) == 0);
        CHECK(us_init(params) == (int32_t)0x80960003u);
        int32_t user = -7, list[4] = {-7, -7, -7, -7};
        CHECK(us_initial_user(&user) == 0 && user != -1 && user != 0xff && user != -7);
        CHECK(us_login_list(list) == 0 && list[0] == user && list[1] == -1 && list[2] == -1 && list[3] == -1);
        CHECK(us_terminate() == 0);
        CHECK(us_init(params) == 0);    /* initialized again after a terminate */
        CHECK(us_terminate() == 0);
    }

    /* libSceCommonDialog: the first initialize succeeds, a repeat answers 0x80b80002 as the
       executable's second site expects. */
    const f_us_none cd_init = (f_us_none)fn("libSceCommonDialog", "sceCommonDialogInitialize");
    if (cd_init) {
        CHECK(cd_init() == 0);
        CHECK(cd_init() == (int32_t)0x80b80002u);
    }

    /* libSceSystemService: the language the person running the runtime chose. */
    sys_param = (f_sys_param)fn("libSceSystemService", "sceSystemServiceParamGetInt");
    if (sys_param) {
        rt_process.system_params[0] = (rt_system_param){1, 18};
        rt_process.system_param_count = 1;
        int32_t value = -1;
        CHECK(sys_param(1, &value) == 0 && value == 18);
        CHECK(died_in_fatal(sys_param_unset));
        CHECK(died_in_fatal(sys_param_no_variable));
        rt_process.system_param_count = 0;
    }

    /* sceNpSetContentRestriction: offline, nothing to restrict; a missing block stops by name. */
    np_restrict = (f_np_restrict)fn("libSceNpManager", "sceNpSetContentRestriction");
    if (np_restrict) {
        const uint8_t params[0x20] = {0};
        CHECK(np_restrict(params) == 0);
        CHECK(died_in_fatal(np_restrict_null));
    }

    /* sceKernelMapFlexibleMemory: zeroed, writable memory; GPU access accepted. */
    flex_map = (f_flex_map)fn("libkernel", "sceKernelMapFlexibleMemory");
    if (flex_map) {
        void *a = NULL, *b = NULL;
        CHECK(flex_map(&a, 0x8000, 3, 0) == 0 && a);
        CHECK(flex_map(&b, 0x4000, 0x33, 0) == 0 && b && b != a);
        if (a) {
            volatile unsigned char *m = a;
            CHECK(m[0] == 0 && m[0x7fff] == 0);
            m[0x7fff] = 7;
            CHECK(m[0x7fff] == 7);
        }
        CHECK(died_in_fatal(flex_no_variable));
        CHECK(died_in_fatal(flex_hint));
        CHECK(died_in_fatal(flex_flags));
        CHECK(died_in_fatal(flex_partial_page));
        CHECK(died_in_fatal(flex_exec));
        /* sceKernelQueryMemoryProtection: the protection asked for, GPU bits included. */
        typedef int32_t (*f_qmp)(const void *, void **, void **, int32_t *);
        const f_qmp qmp = (f_qmp)fn("libkernel", "sceKernelQueryMemoryProtection");
        if (qmp && a && b) {
            int32_t prot = -1;
            CHECK(qmp((char *)a + 0x7fff, NULL, NULL, &prot) == 0 && prot == 3);
            CHECK(qmp(b, NULL, NULL, &prot) == 0 && prot == 0x33);
            qmp_fn = qmp;
            CHECK(died_in_fatal(qmp_unmapped));
            CHECK(died_in_fatal(qmp_with_end));
        }
    }

    /* sceNetInit / sceNetTerm / sceNetPoolCreate. */
    const f_net_none net_init_fn = (f_net_none)fn("libSceNet", "sceNetInit");
    net_term_fn = (f_net_none)fn("libSceNet", "sceNetTerm");
    net_pool_fn = (f_net_pool)fn("libSceNet", "sceNetPoolCreate");
    if (net_init_fn && net_term_fn && net_pool_fn) {
        CHECK(died_in_fatal(net_term_uninitialized));
        CHECK(died_in_fatal(net_pool_uninitialized));
        CHECK(net_init_fn() == 0);
        CHECK(net_init_fn() == (int32_t)0x80410110u);
        const int32_t a = net_pool_fn("FD4HttpMan", 0x4000, 0), b = net_pool_fn("other", 0x1000, 0);
        CHECK(a >= 0 && b >= 0 && a != b);
        CHECK(died_in_fatal(net_pool_flags));
        CHECK(died_in_fatal(net_pool_no_name));
        /* The HTTP manager's order: pool, SSL context, HTTP context over both. */
        const f_ssl_init ssl_init_fn = (f_ssl_init)fn("libSceSsl", "sceSslInit");
        http_init_fn = (f_http_init)fn("libSceHttp", "sceHttpInit");
        if (ssl_init_fn && http_init_fn) {
            http_test_pool = a;
            http_test_ssl = ssl_init_fn(0x4000);
            CHECK(http_test_ssl >= 0 && ssl_init_fn(0x4000) != http_test_ssl);
            const int32_t http = http_init_fn(a, http_test_ssl, 0x4000);
            CHECK(http >= 0 && http_init_fn(b, http_test_ssl, 0x4000) != http);
            CHECK(died_in_fatal(http_init_unknown_pool));
            CHECK(died_in_fatal(http_init_unknown_ssl));
            /* Templates over an HTTP context: the executable's call passes HTTP/1.1 and proxy 1. */
            http_template_fn = (f_http_template)fn("libSceHttp", "sceHttpCreateTemplate");
            if (http_template_fn) {
                http_test_context = http;
                const int32_t t1 = http_template_fn(http, "agent", 2, 1);
                CHECK(t1 >= 0 && http_template_fn(http, "agent", 1, 1) != t1);
                CHECK(died_in_fatal(http_template_unknown_context));
                CHECK(died_in_fatal(http_template_no_agent));
                CHECK(died_in_fatal(http_template_version));
                CHECK(died_in_fatal(http_template_proxy));
                http_nonblock_fn = (f_http_nonblock)fn("libSceHttp", "sceHttpSetNonblock");
                if (http_nonblock_fn) {
                    CHECK(http_nonblock_fn(t1, 1) == 0 && http_nonblock_fn(t1, 0) == 0);
                    CHECK(died_in_fatal(http_nonblock_unknown));
                    CHECK(died_in_fatal(http_nonblock_value));
                }
                http_epoll_fn = (f_http_epoll)fn("libSceHttp", "sceHttpCreateEpoll");
                if (http_epoll_fn) {
                    void *h1 = NULL, *h2 = NULL;
                    CHECK(http_epoll_fn(http, &h1) == 0 && h1);
                    CHECK(http_epoll_fn(http, &h2) == 0 && h2 && h2 != h1);
                    CHECK(died_in_fatal(http_epoll_unknown));
                    CHECK(died_in_fatal(http_epoll_no_slot));
                }
                http_header_fn = (f_http_header)fn("libSceHttp", "sceHttpAddRequestHeader");
                if (http_header_fn) {
                    CHECK(http_header_fn(t1, "Accept", "*/*", 0) == 0);
                    CHECK(died_in_fatal(http_header_unknown));
                    CHECK(died_in_fatal(http_header_no_value));
                    CHECK(died_in_fatal(http_header_mode));
                }
            }
        }
        CHECK(net_term_fn() == 0);
        CHECK(net_init_fn() == 0);  /* initialized again after a terminate */
        CHECK(net_term_fn() == 0);
    }

    /* clock_gettime: CLOCK_SECOND (13) is the wall clock in whole seconds; 0 and 4 as FreeBSD. */
    clock_gettime_fn = (f_clock_gettime)fn("libkernel", "clock_gettime");
    if (clock_gettime_fn) {
        int64_t second[2] = {-1, -1}, real[2] = {-1, -1}, mono1[2], mono2[2];
        CHECK(clock_gettime_fn(13, second) == 0 && second[1] == 0);
        CHECK(clock_gettime_fn(0, real) == 0 && real[1] >= 0 && real[1] < 1000000000);
        CHECK(second[0] > 1600000000 && (real[0] == second[0] || real[0] == second[0] + 1));
        CHECK(clock_gettime_fn(4, mono1) == 0 && clock_gettime_fn(4, mono2) == 0);
        CHECK(mono2[0] > mono1[0] || (mono2[0] == mono1[0] && mono2[1] >= mono1[1]));
        CHECK(died_in_fatal(clock_unknown));
        CHECK(died_in_fatal(clock_no_timespec));
    }
    /* sceKernelClockGettime: the same clocks; errors as SCE codes (EINVAL, EFAULT). */
    const f_clock_gettime sce_clock = (f_clock_gettime)fn("libkernel", "sceKernelClockGettime");
    if (sce_clock) {
        int64_t mono[2] = {-1, -1};
        CHECK(sce_clock(4, mono) == 0 && mono[0] >= 0 && mono[1] >= 0 && mono[1] < 1000000000);
        CHECK(sce_clock(1, mono) == (int)0x80020016);
        CHECK(sce_clock(4, NULL) == (int)0x8002000e);
    }

    /* sceKernelGetProcessTime: microseconds since the process started, never going back. */
    const f_u64 process_time = (f_u64)fn("libkernel", "sceKernelGetProcessTime");
    if (process_time) {
        const uint64_t t1 = process_time();
        struct timespec pause = {0, 20 * 1000 * 1000};
        nanosleep(&pause, NULL);
        const uint64_t t2 = process_time();
        CHECK(t1 < 600ull * 1000000 && t2 >= t1 + 15000 && t2 < t1 + 5000000); /* 20 ms later, in µs */
    }

    /* sceAjmInitialize / sceAjmModuleRegister. */
    ajm_init_fn = (f_ajm_init)fn("libSceAjm", "sceAjmInitialize");
    ajm_register_fn = (f_ajm_register)fn("libSceAjm", "sceAjmModuleRegister");
    if (ajm_init_fn && ajm_register_fn) {
        uint32_t context = 0xffffffffu;
        CHECK(ajm_init_fn(0, &context) == 0 && context != 0xffffffffu && context != 0);
        ajm_test_context = context;
        CHECK(died_in_fatal(ajm_init_reserved));
        CHECK(died_in_fatal(ajm_register_unknown_context));
        CHECK(died_in_fatal(ajm_register_other_module));
        CHECK(ajm_register_fn(context, 1, 0) == 0);
        CHECK(died_in_fatal(ajm_register_twice));
    }

    /* sceAudioOutInit / sceAudioOutOpen. */
    const f_ao_init ao_init_fn = (f_ao_init)fn("libSceAudioOut", "sceAudioOutInit");
    ao_open_fn = (f_ao_open)fn("libSceAudioOut", "sceAudioOutOpen");
    if (ao_init_fn && ao_open_fn) {
        CHECK(ao_open_fn(0xff, 0, 0, 256, 48000, 4) == (int32_t)0x8026000Fu); /* before init */
        CHECK(ao_init_fn() == 0 && ao_init_fn() == (int32_t)0x8026000Eu);
        const int32_t h1 = ao_open_fn(0xff, 0, 0, 256, 48000, 4), h2 = ao_open_fn(0xff, 0, 0, 256, 48000, 4);
        CHECK(h1 > 0 && h2 > 0 && h1 != h2);
        CHECK(ao_open_fn(0xff, 0, 0, 256, 48000, 5) > 0); /* eight channels, as the boot opens */
        CHECK(died_in_fatal(ao_open_s16));
        CHECK(died_in_fatal(ao_open_other_rate));
        CHECK(died_in_fatal(ao_open_other_user));
        CHECK(died_in_fatal(ao_open_port_type_2));
        ao_volume_fn = (f_ao_volume)fn("libSceAudioOut", "sceAudioOutSetVolume");
        if (ao_volume_fn) {
            const int32_t eight = ao_open_fn(0xff, 0, 0, 256, 48000, 5);
            CHECK(ao_volume_fn(eight, 0xff, ao_unity) == 0); /* the boot's call */
            CHECK(ao_open_fn(0xff, 1, 0, 256, 48000, 5) > 0); /* the boot's second port */
            ao_stereo_port = h1;
            CHECK(ao_volume_fn(h1, 0x3, ao_unity) == 0);
            CHECK(died_in_fatal(ao_volume_extra_channel));
            CHECK(died_in_fatal(ao_volume_unknown_port));
            CHECK(died_in_fatal(ao_volume_too_loud));
        }
        /* sceAudioOutOutput paces the caller: 20 blocks of 256 samples at 48 kHz take ~107 ms. */
        const f_ao_output ao_output = (f_ao_output)fn("libSceAudioOut", "sceAudioOutOutput");
        if (ao_output) {
            static float block[256 * 8];
            const int32_t port = ao_open_fn(0xff, 0, 0, 256, 48000, 5);
            CHECK(ao_output(port, block) == 0); /* an idle port takes the first block at once */
            struct timespec start;
            clock_gettime(CLOCK_MONOTONIC, &start);
            for (int i = 0; i < 20; i++) CHECK(ao_output(port, block) == 0);
            const long elapsed = elapsed_us(&start);
            CHECK(elapsed >= 100000 && elapsed < 300000);
            ao_output_fn = ao_output;
            CHECK(died_in_fatal(ao_output_no_samples));
        }
        /* sceAudioOutGetPortState: nothing is heard, so output (the first 16 bits) is 0. */
        typedef int32_t (*f_ao_state)(int32_t, void *);
        const f_ao_state ao_state = (f_ao_state)fn("libSceAudioOut", "sceAudioOutGetPortState");
        if (ao_state) {
            unsigned char state[32];
            memset(state, 0xee, sizeof state);
            CHECK(ao_state(h1, state) == 0 && state[0] == 0 && state[1] == 0 && state[31] == 0);
        }
    }

    /* sceKernelStat: FreeBSD's 120-byte stat from the host file; ENOENT and ENOTDIR as SCE codes. */
    kstat_fn = (f_kstat)fn("libkernel", "sceKernelStat");
    if (kstat_fn) {
        char dir[] = "/tmp/pb-app0-XXXXXX";
        const char *tmp = getenv("TMPDIR");
        static char dir_buf[512];
        snprintf(dir_buf, sizeof dir_buf, "%s/pb-app0-XXXXXX", tmp ? tmp : "/tmp");
        char *app0 = mkdtemp(tmp ? dir_buf : dir);
        CHECK(app0 != NULL);
        if (app0) {
            char file[600];
            snprintf(file, sizeof file, "%s/data.bin", app0);
            FILE *f = fopen(file, "w");
            if (f) { fwrite("0123456789", 1, 10, f); fclose(f); }
            chmod(file, 0644);
            rt_process.app0_dir = app0;
            unsigned char st[120];
            memset(st, 0xee, sizeof st);
            CHECK(kstat_fn("/app0/data.bin", st) == 0);
            uint16_t mode; int64_t size;
            memcpy(&mode, st + 8, 2); memcpy(&size, st + 0x48, 8);
            CHECK((mode & 0170000) == 0100000 && (mode & 0777) == 0444 && size == 10);
            CHECK(kstat_fn("/app0", st) == 0 && (memcpy(&mode, st + 8, 2), (mode & 0170000) == 0040000));
            CHECK(kstat_fn("/app0/missing.bin", st) == (int32_t)0x80020002u);
            CHECK(kstat_fn("/app0/data.bin/inside", st) == (int32_t)0x80020014u);
            CHECK(died_in_fatal(kstat_outside));
            CHECK(died_in_fatal(kstat_dotdot));
            CHECK(died_in_fatal(kstat_prefix_only));
            /* Open, read, seek, pread, fstat and close the same file, read-only. */
            typedef int32_t (*f_open)(const char *, int32_t, uint32_t);
            typedef int64_t (*f_read)(int32_t, void *, uint64_t);
            typedef int64_t (*f_pread)(int32_t, void *, uint64_t, int64_t);
            typedef int64_t (*f_lseek)(int32_t, int64_t, int32_t);
            typedef int32_t (*f_fd)(int32_t);
            typedef int32_t (*f_fstat)(int32_t, void *);
            const f_open kopen = (f_open)fn("libkernel", "sceKernelOpen");
            const f_read kread = (f_read)fn("libkernel", "sceKernelRead");
            const f_pread kpread = (f_pread)fn("libkernel", "sceKernelPread");
            const f_lseek klseek = (f_lseek)fn("libkernel", "sceKernelLseek");
            const f_fd kclose = (f_fd)fn("libkernel", "sceKernelClose");
            const f_fstat kfstat = (f_fstat)fn("libkernel", "sceKernelFstat");
            if (kopen && kread && kpread && klseek && kclose && kfstat) {
                const int32_t fd = kopen("/app0/data.bin", 0, 0555);
                CHECK(fd >= 3);
                char buf[16] = {0};
                CHECK(kread(fd, buf, 4) == 4 && !memcmp(buf, "0123", 4));
                CHECK(kread(fd, buf, 16) == 6 && !memcmp(buf, "456789", 6));
                CHECK(kread(fd, buf, 16) == 0);
                CHECK(klseek(fd, 2, 0) == 2 && kread(fd, buf, 2) == 2 && !memcmp(buf, "23", 2));
                CHECK(klseek(fd, -1, 2) == 9 && klseek(fd, 1, 1) == 10);
                CHECK(klseek(fd, 0, 7) == (int32_t)0x80020016u && klseek(fd, -1, 0) == (int32_t)0x80020016u); /* sign-extended */
                CHECK(kpread(fd, buf, 3, 7) == 3 && !memcmp(buf, "789", 3));
                memset(st, 0, sizeof st);
                CHECK(kfstat(fd, st) == 0 && (memcpy(&size, st + 0x48, 8), size == 10));
                const int32_t fd2 = kopen("/app0/data.bin", 0, 0);
                CHECK(fd2 > 0 && fd2 != fd);
                CHECK(kclose(fd) == 0 && kclose(fd) == (int32_t)0x80020009u);
                CHECK(kread(fd, buf, 1) == (int32_t)0x80020009u);
                CHECK(kopen("/app0/data.bin", 0, 0) == fd); /* the lowest free number again */
                CHECK(kclose(fd) == 0 && kclose(fd2) == 0);
                CHECK(kopen("/app0/missing.bin", 0, 0) == (int32_t)0x80020002u);
                CHECK(kopen("/app0/data.bin", 2, 0) == (int32_t)0x8002001eu);     /* O_RDWR: EROFS */
                CHECK(kopen("/app0/new.bin", 0x201, 0644) == (int32_t)0x8002001eu); /* O_CREAT */
                CHECK(kopen("/app0/missing.bin", 1, 0) == (int32_t)0x80020002u);   /* lookup first */
                CHECK(access(file, F_OK) == 0); /* nothing was written or created */
            }
            unlink(file);
            rmdir(app0);
            rt_process.app0_dir = NULL;
        }
    }

    if (!failures) puts("syslib: ok");
    return failures != 0;
}
