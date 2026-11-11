/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The runtime's system library implementations, called the way the guest calls them: through the
   RT_SYSLIB registry, by library and symbol. */
#include "syslib_test.h"

#include "runtime/ajm.h"
#include "runtime/audio_host.h"
#include "runtime/display.h"
#include "runtime/equeue.h"
#include "runtime/gpu_memory.h"
#include "runtime/http.h"
#include "runtime/np.h"
#include "runtime/pad.h"
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/system_service.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int unresolved_test_entry(void) { return -1; }
RT_SYSLIB_UNRESOLVED("test", testUnresolvedEntry, unresolved_test_entry);

int failures;

/* The tests in test/syslib/, each in its own file (SYSLIB_TEST), run after main's own. */
static syslib_test *registered_tests;

void syslib_test_register(syslib_test *test)
{
    syslib_test **at = &registered_tests;
    while (*at && strcmp((*at)->name, test->name) < 0) at = &(*at)->next;  /* by name: a fixed order */
    test->next = *at;
    *at = test;
}

void *fn(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, library) && !strcmp(e->symbol, symbol)) return e->fn;
    fprintf(stderr, "no %s %s\n", library, symbol);
    failures++;
    return NULL;
}

int is_implemented(const char *library, const char *symbol)
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
typedef int32_t (*f_sysmodule)(uint32_t);

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

/* Mutex ownership. libthr answers EPERM to a caller that does not own the mutex, for every type, so
   a second host thread unlocks and waits while the main thread holds it; it needs no guest thread,
   only the mutex and cond objects the SCE calls made. */
static finit owner_init;
static f1 owner_lock, owner_posix_lock, owner_trylock, owner_unlock, owner_ainit;
static f2i owner_settype;
static f_cond_wait owner_cond_wait;
static void *owner_mutex, *owner_cond;
static int owner_other_trylock, owner_other_unlock, owner_other_wait;
static void *owner_other_thread(void *unused)
{
    (void)unused;
    owner_other_trylock = owner_trylock(&owner_mutex);
    owner_other_unlock = owner_unlock(&owner_mutex);
    owner_other_wait = owner_cond_wait(&owner_cond, &owner_mutex);
    return NULL;
}
/* A normal mutex locked again by its owner: libthr waits forever, so the runtime stops by name.
   Each child makes its own mutex: one copied from the parent would name the parent's thread. */
static void lock_normal_twice(f1 relock)
{
    void *attr = NULL, *mutex = NULL;
    if (owner_ainit(&attr) || owner_settype(&attr, 3) || owner_init(&mutex, &attr, NULL)) return;
    if (owner_lock(&mutex) == 0) relock(&mutex);
}
static void lock_normal_twice_sce(void) { lock_normal_twice(owner_lock); }
static void lock_normal_twice_posix(void) { lock_normal_twice(owner_posix_lock); }

typedef uint64_t (*attribute_wire)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static attribute_wire attribute;
static void video_attribute_null(void) { attribute(NULL, 0, 0, 0, 0, 0, 0); }

int died_in_fatal(void (*call)(void))
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
static unsigned int (*posix_sleep_entry)(unsigned int);
static void sleep_above_int_max(void) { posix_sleep_entry(0x80000000u); } /* INT_MAX + 1 */

/* The guest's struct timespec: two 64-bit fields, seconds and nanoseconds (FreeBSD amd64). */
typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} nanosleep_timespec;
static int (*nanosleep_entry)(const nanosleep_timespec *, nanosleep_timespec *);
static void nanosleep_ask(int64_t seconds, int64_t nanoseconds)
{
    nanosleep_timespec request = {seconds, nanoseconds};
    nanosleep_entry(&request, NULL);
}
static void nanosleep_no_request(void) { nanosleep_entry(NULL, NULL); }
static void nanosleep_negative_nanoseconds(void) { nanosleep_ask(0, -1); }
static void nanosleep_one_billion_nanoseconds(void) { nanosleep_ask(0, 1000000000); }
static void nanosleep_negative_seconds(void) { nanosleep_ask(-1, 0); }
/* The host answers each of these requests with EINVAL too, which also stops the process, so the
   child checks which stop it reached (the observer pattern of test_direct_memory_bounds.c). */
static const char *nanosleep_expected_reason;
static void nanosleep_observe_stop(const char *function, const char *reason)
{
    _exit(!strcmp(function, "nanosleep") && strstr(reason, nanosleep_expected_reason) ? 0 : 1);
}
static int nanosleep_stops_with(void (*call)(void), const char *reason)
{
    const pid_t child = fork();
    if (child == 0) {
        nanosleep_expected_reason = reason;
        rt_fatal_observer = nanosleep_observe_stop;
        call();
        _exit(2); /* the call returned, so it did not stop */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

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
static void nat_info_other_size(void) { int nat[4] = {12, 0, 0, 0}; netctl_nat(nat); }
static void nat_info_null(void) { netctl_nat(NULL); }

/* libSceSysmodule: a stub module (one the title ships but the runtime does not map) loads as done.
   The ids here are made up; the real ones come from the game's data through runtime/boot.c. */
enum { STUB_ID = 0x7f01, OTHER_STUB_ID = 0x7f02, NOT_A_STUB_ID = 0x7f03 };
static f_sysmodule sysmodule_load, sysmodule_is_loaded, sysmodule_unload;
static void load_not_a_stub(void) { sysmodule_load(NOT_A_STUB_ID); }
static void is_loaded_not_a_stub(void) { sysmodule_is_loaded(NOT_A_STUB_ID); }
static void unload_not_a_stub(void) { sysmodule_unload(NOT_A_STUB_ID); }
static void is_loaded_other_stub(void) { sysmodule_is_loaded(OTHER_STUB_ID); }
static void unload_other_stub(void) { sysmodule_unload(OTHER_STUB_ID); }
static void unload_wide_stub(void) { sysmodule_unload(UINT32_C(0xabcd0000) | STUB_ID); }

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
static void *dm_in_use;
static void dm_map_hint_in_use(void) { void *a = dm_in_use; dm_map(&a, DM_PAGE, DM_RW, 0, dm_mapped, DM_PAGE); }
typedef int32_t (*f_vq)(const void *, int32_t, void *, uint64_t);
static f_vq vq_fn;
static void vq_flags(void) { unsigned char info[0x48]; vq_fn(dm_in_use, 1, info, sizeof info); }
static void vq_size(void) { unsigned char info[0x48]; vq_fn(dm_in_use, 0, info, 0x40); }
static void dm_map_hint_misaligned(void) { void *a = (void *)0xfd00004000; dm_map(&a, DM_PAGE, DM_RW, 0, dm_mapped, DM_ALIGN); }
static void dm_map_exec(void) { void *a = NULL; dm_map(&a, DM_PAGE, DM_RW | 0x4, 0, dm_unmapped, DM_PAGE); }
static void dm_map_gpu_twice(void) { void *a = NULL; dm_map(&a, DM_PAGE, DM_RW | 0x30, 0, dm_unmapped, DM_PAGE); }
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
static void *sema_shared;
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
/* The runtime's semaphore object, mirrored (runtime/syslib/kernel_sema.c) so a test can see how
   many waiters are inside the wait before it signals; keep the two in step. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int32_t count, max;
    uint32_t attr;
    uint64_t next_ticket;
    void *queue;
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
/* pthread_cond_timedwait: an absolute time on the wall clock, a timespec of two 64-bit fields. */
typedef struct {
    int64_t seconds, nanoseconds;
} pc_timespec;
static int (*pc_timedwait)(void **, void **, const pc_timespec *);
static pc_timespec pc_wall_in_ms(int64_t ms)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t ns = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec + ms * 1000000;
    return (pc_timespec){ns / 1000000000, ns % 1000000000};
}
static int pc_wall_reached(pc_timespec at)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec > at.seconds || (now.tv_sec == at.seconds && now.tv_nsec >= at.nanoseconds);
}
static int pc_timed_go, pc_timed_result = -1, pc_other_result = -1;
static void *pc_timed_waiter(void *unused) /* the latest time there is: only the signal ends it */
{
    (void)unused;
    const pc_timespec later = {INT64_MAX, 999999999};
    pc_lock(&pc_mutex);
    int r = 0;
    while (!pc_timed_go && !r) r = pc_timedwait(&pc_cond, &pc_mutex, &later);
    pc_timed_result = r;
    pc_unlock(&pc_mutex);
    return NULL;
}
static void *pc_other_waiter(void *unused) /* the mutex belongs to the main thread */
{
    (void)unused;
    const pc_timespec later = pc_wall_in_ms(10000);
    pc_other_result = pc_timedwait(&pc_cond, &pc_mutex, &later);
    return NULL;
}

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
/* Thread-specific data keys: each thread's own values, destructors when a created thread ends. */
typedef int (*f_key_create)(int32_t *, void (*)(void *));
typedef int (*f_key)(int32_t);
typedef void *(*f_key_get)(int32_t);
typedef int (*f_key_set)(int32_t, const void *);
static f_key_create kc_create;
static f_key kc_delete;
static f_key_get kc_get;
static f_key_set kc_set;
static int32_t kc_key, kc_plain_key, kc_deleted_key, kc_endless_key;
static int kc_marker;
static void *kc_seen_before, *kc_seen_in_destructor, *kc_destroyed[4];
static atomic_int kc_calls, kc_deleted_calls, kc_done;
/* Sets the value once more on its first call, so a second round calls it again. */
static void kc_destructor(void *value)
{
    const int n = atomic_load(&kc_calls);
    if (n < 4) kc_destroyed[n] = value;
    kc_seen_in_destructor = kc_get(kc_key);
    if (n == 0) kc_set(kc_key, value);
    atomic_store(&kc_calls, n + 1);
    if (n == 1) atomic_store(&kc_done, 1);
}
static void kc_deleted_destructor(void *value) { (void)value; atomic_fetch_add(&kc_deleted_calls, 1); }
static void *kc_entry(void *argument)
{
    kc_seen_before = kc_get(kc_key);
    kc_set(kc_key, argument);
    kc_set(kc_plain_key, argument);    /* no destructor: only cleared */
    kc_set(kc_deleted_key, argument);
    kc_delete(kc_deleted_key);         /* deleted while set: its destructor is not called */
    return NULL;
}
static void kc_endless_destructor(void *value) { kc_set(kc_endless_key, value); }
static void *kc_endless_entry(void *argument)
{
    kc_set(kc_endless_key, argument);
    return NULL;
}
static void kc_endless(void)
{
    void *t = NULL, *a = tc_attr(1, 1, 1);
    if (kc_create(&kc_endless_key, kc_endless_destructor) != 0) return;
    tc_create(&t, &a, kc_endless_entry, &kc_marker, "endless");
    struct timespec pause = {2, 0};
    nanosleep(&pause, NULL); /* the thread's end stops the process before this returns */
}
/* Runs call in a forked child and answers whether the child ended by _exit(0): no stop by name. */
static int kc_exits_cleanly(void (*call)(void))
{
    const pid_t child = fork();
    if (child == 0) {
        call();
        _exit(0);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
/* The limits are pinned from both sides: exactly the documented count works, one more stops.
   Each case runs in a forked child, which first deletes every key the parent left so the child's
   table is empty, whatever keys the earlier tests still hold. */
static void kc_fill_128(void)
{
    for (int32_t k = 1; k <= 128; k++) kc_delete(k);
    for (int i = 0; i < 128; i++) {
        int32_t key = 0;
        if (kc_create(&key, NULL) != 0 || key < 1 || key > 128) _exit(1);
    }
}
static void kc_too_many(void)
{
    kc_fill_128();
    int32_t key;
    kc_create(&key, NULL); /* the 129th live key stops by name */
}
/* A destructor that sets its value again kc_resets times needs kc_resets + 1 rounds. */
static int32_t kc_rounds_key;
static int kc_resets;
static atomic_int kc_rounds_calls;
static void kc_rounds_destructor(void *value)
{
    if (atomic_fetch_add(&kc_rounds_calls, 1) < kc_resets) kc_set(kc_rounds_key, value);
}
static void *kc_rounds_entry(void *argument)
{
    kc_set(kc_rounds_key, argument);
    return NULL;
}
static void kc_rounds(void)
{
    void *t = NULL, *a = tc_attr(1, 1, 1);
    if (kc_create(&kc_rounds_key, kc_rounds_destructor) != 0) _exit(1);
    if (tc_create(&t, &a, kc_rounds_entry, &kc_marker, "rounds") != 0) _exit(1);
    for (int i = 0; i < 100000 && atomic_load(&kc_rounds_calls) < kc_resets + 1; i++) sched_yield();
    struct timespec settle = {0, 100 * 1000 * 1000};
    nanosleep(&settle, NULL); /* room for the stop, or a wrong extra call, after the last one */
    if (atomic_load(&kc_rounds_calls) != kc_resets + 1) _exit(1);
}
static void kc_four_rounds(void) { kc_resets = 3; kc_rounds(); }
static void kc_five_rounds(void) { kc_resets = 4; kc_rounds(); }
static void tc_affinity_empty(void) { tc_affinity(tc_seen_self, 0); }

/* The user service's start-up calls. */
typedef int32_t (*f_us_init)(const uint32_t *);
typedef int32_t (*f_us_none)(void);
typedef int32_t (*f_us_ids)(int32_t *);
static f_us_none us_terminate;
static f_us_ids us_initial_user, us_login_list;
static void us_terminate_uninitialized(void) { us_terminate(); }
static void us_initial_user_uninitialized(void) { int32_t u; us_initial_user(&u); }
typedef int32_t (*f_us_name)(int32_t, char *, size_t);
static f_us_name us_name;
static int32_t us_user;
static void us_name_uninitialized(void) { char n[17]; us_name(1, n, sizeof n); }
static void us_name_other_user(void) { char n[17]; us_name(us_user + 1, n, sizeof n); }
static void us_name_short(void) { char n[6]; us_name(us_user, n, sizeof n); }
static void us_name_no_buffer(void) { us_name(us_user, NULL, 17); }
typedef int32_t (*f_us_event)(int32_t *);
static f_us_event us_event;
static void us_event_uninitialized(void) { int32_t e[2]; us_event(e); }
static void us_event_no_variable(void) { us_event(NULL); }

/* libScePad: one standard controller, the one user's, connected and at rest. */
typedef int32_t (*f_pad_none)(void);
typedef int32_t (*f_pad_open)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_pad_handle)(int32_t);
typedef int32_t (*f_pad_block)(int32_t, void *);
static f_pad_none pad_init;
static f_pad_open pad_open;
static f_pad_handle pad_close;
static f_pad_block pad_read, pad_info;
static int32_t pad_handle;
static void pad_open_before_init(void) { pad_open(1, 0, 0, NULL); }
static void pad_init_again(void) { pad_init(); }
static void pad_open_other_user(void) { pad_open(2, 0, 0, NULL); }
static void pad_open_special(void) { pad_open(1, 2, 0, NULL); }
static void pad_open_index(void) { pad_open(1, 0, 1, NULL); }
static void pad_read_bad_handle(void) { unsigned char d[120]; pad_read(pad_handle + 1, d); }
static void pad_read_no_block(void) { pad_read(pad_handle, NULL); }
static void pad_info_no_block(void) { pad_info(pad_handle, NULL); }
static void pad_read_closed(void) { unsigned char d[120]; pad_read(pad_handle, d); }
typedef int32_t (*f_pad_vibrate)(int32_t, const uint8_t *);
static f_pad_vibrate pad_vibrate;
static void pad_vibrate_missing(void) { pad_vibrate(pad_handle, NULL); }
static void pad_vibrate_closed(void) { const uint8_t s[2] = {1, 2}; pad_vibrate(pad_handle, s); }

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
typedef int32_t (*f_http_wait)(void *, void *, int32_t, int64_t);
static f_http_wait http_wait_fn;
static void *http_wait_handle;
static void http_wait_unknown(void) { static unsigned char events[24]; int marker; http_wait_fn(&marker, events, 1, 0); }
static void http_wait_timeout(void) { static unsigned char events[24]; http_wait_fn(http_wait_handle, events, 1, 1000); }
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

/* libSceAjm: contexts, ATRAC9 instances, and batches of jobs run when started. */
typedef int32_t (*f_ajm_init)(int64_t, uint32_t *);
typedef int32_t (*f_ajm_register)(uint32_t, uint32_t, uint64_t);
typedef int32_t (*f_ajm_create)(uint32_t, uint32_t, uint64_t, uint32_t *);
typedef int32_t (*f_ajm_two)(uint32_t, uint32_t);
typedef void *(*f_ajm_control)(void *, uint32_t, uint64_t, void *, uint64_t, void *, uint64_t, void *);
typedef void *(*f_ajm_run)(void *, uint32_t, uint64_t, void *, uint64_t, void *, uint64_t, void *, uint64_t, void *);
typedef int32_t (*f_ajm_start)(uint32_t, const void *, uint32_t, int32_t, void *, uint32_t *);
typedef int32_t (*f_ajm_wait)(uint32_t, uint32_t, uint32_t, void *);
static f_ajm_run ajm_run_fn;
static f_ajm_start ajm_start_fn;
static uint32_t ajm_test_context, ajm_test_instance;
/* A stand-in ATRAC9 decoder for the tests (no ATRAC9 data may be in the repository): each frame
   reads ajm_fake_frame_bytes and writes 256 stereo samples numbered from ajm_fake_next. */
static int ajm_fake_frame_bytes = 256, ajm_fake_fail;
static int16_t ajm_fake_next;
static int ajm_fake_handle;
static void *ajm_fake_open(const uint8_t config[4], uint32_t *channels) { *channels = 2; return config[0] == 0xFE ? &ajm_fake_handle : NULL; }
static int ajm_fake_decode(void *d, const uint8_t *in, int16_t *pcm, int *used)
{
    (void)d, (void)in;
    if (ajm_fake_fail) return -5;
    for (int k = 0; k < 256 * 2; k++) pcm[k] = ajm_fake_next++;
    *used = ajm_fake_frame_bytes;
    return 0;
}
static void ajm_fake_release(void *d) { (void)d; }
/* Runs one job on ajm_test_instance: a run job with input and output, or a control job. */
static int32_t ajm_job(uint64_t flags, const void *in, uint32_t in_size, void *out, uint32_t out_size, void *sideband, uint32_t sideband_size)
{
    static uint8_t batch[0x60];
    uint32_t id;
    const uint8_t *end = ajm_run_fn(batch, ajm_test_instance, flags, (void *)in, in_size, out, out_size, sideband, sideband_size, NULL);
    const int32_t started = ajm_start_fn(ajm_test_context, batch, (uint32_t)(end - batch), 0, NULL, &id);
    return started ? started : ((int32_t (*)(uint32_t, uint32_t, uint32_t, void *))fn("libSceAjm", "sceAjmBatchWait"))(ajm_test_context, id, 0, NULL);
}

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
static void ao_volume_too_loud(void) { const int32_t v[8] = {40000}; ao_volume_fn(ao_stereo_port, 0x1, v); }

/* sceKernelStat under /app0, served from a scratch directory. */
typedef int32_t (*f_kstat)(const char *, void *);
static f_kstat kstat_fn;
static void kstat_outside(void) { unsigned char st[120]; kstat_fn("/download0/x", st); }
static void kstat_dotdot(void) { unsigned char st[120]; kstat_fn("/app0/../etc/passwd", st); }
typedef int (*f_pstat)(const char *, void *);
typedef int *(*f_errno)(void);
static f_pstat pstat_fn;
static void pstat_outside(void) { unsigned char st[120]; pstat_fn("/download0/x", st); }
static void pstat_no_buffer(void) { pstat_fn("/app0/data.bin", NULL); }
static int *other_thread_errno;
static void *errno_of_thread(void *arg) { other_thread_errno = ((f_errno)arg)(); return NULL; }
/* Whether an address fits a GPU base-address field: below 2^40, so (address >> 8) fits 32 bits. */
static int gpu_addressable(const void *address)
{
    const uint64_t shifted = (uint64_t)(uintptr_t)address >> 8;
    return address && shifted == (uint32_t)shifted;
}
static void kstat_prefix_only(void) { unsigned char st[120]; kstat_fn("/app0x/file", st); }
static void kstat_data_dotdot(void) { unsigned char st[120]; kstat_fn("/data/../etc/passwd", st); }
typedef int32_t (*f_kopen)(const char *, int32_t, uint32_t);
static f_kopen kopen_fn;
static void data_open_odd_flag(void) { kopen_fn("/data/cache.bin", 0x10002, 0777); } /* O_RDWR and an unknown bit */
static void data_open_no_folder(void)
{
    rt_process.data_dir = NULL;
    kopen_fn("/data/cache.bin", 0x602, 0777);
}
/* sceKernelWrite and sceKernelPwrite on a /data file. The stops are checked by reason in a child:
   the host would answer some of these requests with an error, which stops the process too and
   would hide a missing guard. */
typedef int64_t (*f_kwrite)(int32_t, const void *, uint64_t);
typedef int64_t (*f_kpwrite)(int32_t, const void *, uint64_t, int64_t);
static f_kwrite kwrite_fn;
static f_kpwrite kpwrite_fn;
static int32_t write_fd, write_append_fd, write_read_only_fd;
static void write_too_many(void) { kwrite_fn(write_fd, "x", 0x80000000u); } /* INT_MAX + 1 */
static void write_too_many_read_only(void) { kwrite_fn(write_read_only_fd, "x", 0x80000000u); }
static void pwrite_too_many(void) { kpwrite_fn(write_fd, "x", 0x80000000u, 0); }
static void pwrite_too_many_read_only(void) { kpwrite_fn(write_read_only_fd, "x", 0x80000000u, 0); }
static void write_no_buffer(void) { kwrite_fn(write_fd, NULL, 1); }
static void pwrite_no_buffer(void) { kpwrite_fn(write_fd, NULL, 1, 0); }
static void pwrite_append(void) { kpwrite_fn(write_append_fd, "x", 1, 0); }
static const char *write_expected_function, *write_expected_reason;
static void write_observe_stop(const char *function, const char *reason)
{
    _exit(!strcmp(function, write_expected_function) && strstr(reason, write_expected_reason) ? 0 : 1);
}
static int write_stops_with(void (*call)(void), const char *function, const char *reason)
{
    const pid_t child = fork();
    if (child == 0) {
        write_expected_function = function;
        write_expected_reason = reason;
        rt_fatal_observer = write_observe_stop;
        call();
        _exit(2); /* the call returned, so it did not stop */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
/* The answer is the host's count, not the request: in a child, a 4-byte file size limit
   (RLIMIT_FSIZE) makes the host write only up to it. SIGXFSZ is ignored, so the host answers with
   the short count instead of ending the process. */
static int write_returns_short_count(void)
{
    const pid_t child = fork();
    if (child == 0) {
        write_expected_function = write_expected_reason = ""; /* any stop is a failure */
        rt_fatal_observer = write_observe_stop;
        signal(SIGXFSZ, SIG_IGN);
        const struct rlimit limit = {4, 4};
        const int32_t fd = kopen_fn("/data/short.bin", 0x602, 0644);
        if (fd < 3 || setrlimit(RLIMIT_FSIZE, &limit)) _exit(3);
        const int ok = kwrite_fn(fd, "0123456789", 10) == 4 && kpwrite_fn(fd, "abcdefgh", 8, 2) == 2;
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* GNM compute queues. */
typedef int32_t (*f_cq_t)(uint32_t, uint32_t, void *, uint32_t, volatile uint32_t *);
static f_cq_t cq_fn;
static void cq_twice(void) { static uint32_t r[16]; static volatile uint32_t p; cq_fn(0, 0, r, 16, &p); }
static void cq_no_ring(void) { static volatile uint32_t p; cq_fn(1, 0, NULL, 16, &p); }

/* sceGnmDingDong test fixtures: a 4-dword INDIRECT_BUFFER ring packet, as the engine's writer at
   0x0148b330 builds it, pointing at a command buffer. */
typedef void (*f_ding_dong)(uint32_t, uint32_t);
static f_ding_dong ding_dong_fn;
static int32_t dd_queue;
static uint32_t *dd_ring;
static void put_indirect_buffer(uint32_t *at, const uint32_t *buffer, uint32_t dwords)
{
    const uint64_t address = (uint64_t)(uintptr_t)buffer;
    at[0] = 0xc0023f02u;
    at[1] = (uint32_t)address;
    at[2] = (uint32_t)(address >> 32) & 0xffffu;
    at[3] = dwords | 0x1800000u;
}
static void dd_unknown_packet(void)
{
    static uint32_t buffer[2] = {0xc0002400u, 0}; /* opcode 0x24, not one the engine was seen to write */
    put_indirect_buffer(dd_ring + 4, buffer, 2); /* the parent's ring has completed up to slot 4 */
    ding_dong_fn((uint32_t)dd_queue, 8);
}
static void dd_bad_queue(void) { ding_dong_fn(999, 4); }

/* Kernel event queues: a wait blocks until a runtime event source triggers a registration. */
typedef int32_t (*f_eq_create)(rt_equeue **, const char *);
typedef int32_t (*f_eq_delete)(rt_equeue *);
typedef int32_t (*f_eq_wait)(rt_equeue *, void *, int32_t, int32_t *, const void *);
static f_eq_wait eq_wait_fn;
static rt_equeue *eq_test_queue;
static void *eq_trigger_later(void *unused)
{
    (void)unused;
    struct timespec pause = {0, 20 * 1000 * 1000};
    nanosleep(&pause, NULL);
    rt_equeue_trigger(eq_test_queue, RT_EVFILT_DISPLAY, 6, 42);
    return NULL;
}
static void eq_wait_timeout(void) { unsigned char ev[32]; int32_t n; uint32_t usec = 10; eq_wait_fn(eq_test_queue, ev, 1, &n, &usec); }
static void eq_wait_no_count(void) { unsigned char ev[32]; eq_wait_fn(eq_test_queue, ev, 1, NULL, NULL); }

/* Video out: open, buffers, an hsync flip that completes at once and wakes the flip queue. */
typedef int32_t (*f_vo_open)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_vo_rate)(int32_t, int32_t);
typedef int32_t (*f_vo_event)(rt_equeue *, int32_t, void *);
typedef void (*f_vo_attr)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*f_vo_register)(int32_t, int32_t, void *const *, int32_t, const void *);
typedef int32_t (*f_vo_flip)(int32_t, int32_t, int32_t, int64_t);
typedef int32_t (*f_vo_status)(int32_t, void *);
static f_vo_flip vo_flip_fn;
static f_vo_status vo_status_fn;
static f_vo_open vo_open_fn;
static int32_t vo_handle;
static f_create tc_create_posix;
static void *posix_thread_entry(void *argument) { return argument; }
static void posix_create_without_attr(void)
{
    void *thread = NULL;
    tc_create_posix(&thread, NULL, posix_thread_entry, NULL, "worker");
}
static void vo_flip_other_mode(void) { vo_flip_fn(vo_handle, 0, 3, 0); }
typedef int32_t (*f_gnm_submit)(uint32_t, void **, uint32_t *, void **, uint32_t *);
static f_gnm_submit gnm_submit_fn;
static int32_t gnm_submit_one_result(uint32_t *packets, uint32_t dwords)
{
    void *buffers[1] = {packets};
    uint32_t bytes[1] = {dwords * 4};
    return gnm_submit_fn(1, buffers, bytes, NULL, NULL);
}
static void gnm_submit_one(uint32_t *packets, uint32_t dwords) { gnm_submit_one_result(packets, dwords); }
static uint32_t gnm_constant_packets[2] = {0xc0002400u, 0};
static void gnm_submit_with_constant(void)
{
    static uint32_t draw[2] = {0xc0001000u, 0}; /* NOP */
    void *buffers[1] = {draw}, *constants[1] = {gnm_constant_packets};
    uint32_t bytes[1] = {8}, constant_bytes[1] = {8};
    gnm_submit_fn(1, buffers, bytes, constants, constant_bytes);
}
static void gnm_submit_constant_without_sizes(void)
{
    static uint32_t draw[2] = {0xc0001000u, 0};
    void *buffers[1] = {draw}, *constants[1] = {NULL};
    uint32_t bytes[1] = {8};
    gnm_submit_fn(1, buffers, bytes, constants, NULL);
}
static void gnm_submit_dma_fill(void)
{
    /* DMA_DATA filling memory with a value (source select 2): the stand-in does not fill */
    static uint32_t target[4];
    const uint64_t at = (uint64_t)(uintptr_t)target;
    uint32_t p[7] = {0xc0055000u, 0x40000000u, 0x12345678u, 0, (uint32_t)at, (uint32_t)(at >> 32), 16};
    gnm_submit_one(p, 7);
}
static void gnm_submit_dma_other_register(void)
{
    static uint32_t source[25];
    const uint64_t from = (uint64_t)(uintptr_t)source;
    uint32_t p[7] = {0xc0055000u, 0x60000000u, (uint32_t)from, (uint32_t)(from >> 32), 0x30230u, 0, 0x28200064u};
    gnm_submit_one(p, 7);
}
static void gnm_submit_unknown(void) { static uint32_t p[2] = {0xc0002400u, 0}; gnm_submit_one(p, 2); } /* opcode 0x24 */
static uint32_t gnm_wait_label;
static void gnm_submit_unmet_wait(void)
{
    /* WAIT_REG_MEM, memory, "equal", reference 5, while the label holds 0 */
    const uint64_t at = (uint64_t)(uintptr_t)&gnm_wait_label;
    uint32_t p[7] = {0xc0053c00u, 0x13, (uint32_t)at, (uint32_t)(at >> 32), 5, 0xffffffffu, 10};
    gnm_wait_label = 0;
    gnm_submit_one(p, 7);
}
static void vo_flip_bad_buffer(void) { vo_flip_fn(vo_handle, 9, 2, 0); }
static void vo_open_other(void) { vo_open_fn(0xff, 0, 0, NULL); }

/* libSceRtc: the host's local time, and ticks in microseconds since 0001-01-01. */
typedef struct { uint16_t year, month, day, hour, minute, second; uint32_t microsecond; } rtc_time;
typedef int32_t (*f_rtc_now)(rtc_time *);
typedef int32_t (*f_rtc_tick)(const rtc_time *, uint64_t *);
typedef int32_t (*f_rtc_resolution)(void);
static f_rtc_now rtc_now;
static f_rtc_tick rtc_tick;
static void rtc_now_nothing(void) { rtc_now(NULL); }
static void rtc_tick_february_30(void) { const rtc_time t = {2026, 2, 30, 0, 0, 0, 0}; uint64_t k; rtc_tick(&t, &k); }
static void rtc_tick_leap_2100(void) { const rtc_time t = {2100, 2, 29, 0, 0, 0, 0}; uint64_t k; rtc_tick(&t, &k); }
static void rtc_tick_microsecond(void) { const rtc_time t = {2026, 1, 1, 0, 0, 0, 1000000}; uint64_t k; rtc_tick(&t, &k); }
typedef int32_t (*f_rtc_set)(rtc_time *, const uint64_t *);
static f_rtc_set rtc_set;
static void rtc_set_year_10000(void) { rtc_time t; const uint64_t k = 315537897600000000ull; rtc_set(&t, &k); } /* 10000-01-01 */
static void rtc_set_no_tick(void) { rtc_time t; rtc_set(&t, NULL); }
typedef int32_t (*f_rtc_weekday)(int32_t, int32_t, int32_t);
static f_rtc_weekday rtc_weekday;
static void rtc_weekday_february_29_2023(void) { rtc_weekday(2023, 2, 29); }
static void rtc_weekday_month_13(void) { rtc_weekday(2026, 13, 1); }
static void rtc_tick_no_variable(void) { const rtc_time t = {2026, 1, 1, 0, 0, 0, 0}; rtc_tick(&t, NULL); }

/* PlayGo (libScePlayGo): the title's own calls, against a package manifest of three chunks. */
typedef int32_t (*f_pg_init)(const void *);
typedef int32_t (*f_pg_open)(int32_t *, const void *);
typedef int32_t (*f_pg_chunks)(int32_t, uint16_t *, uint32_t, uint32_t *);
typedef int32_t (*f_pg_locus)(int32_t, const uint16_t *, uint32_t, int8_t *);
static f_pg_init pg_init;
static f_pg_open pg_open;
static f_pg_chunks pg_chunks;
static f_pg_locus pg_locus;
static int32_t pg_handle;
static void pg_open_first(void) { int32_t h; pg_open(&h, NULL); }
static void pg_init_nothing(void) { pg_init(NULL); }
static void pg_open_with_params(void) { int32_t h; const int params = 1; pg_open(&h, &params); }
static void pg_chunks_no_manifest(void) { uint16_t ids[100]; uint32_t n; pg_chunks(pg_handle, ids, 100, &n); }
static void pg_chunks_short(void) { uint16_t ids[2]; uint32_t n; pg_chunks(pg_handle, ids, 2, &n); }
static void pg_chunks_bad_handle(void) { uint16_t ids[100]; uint32_t n; pg_chunks(pg_handle + 1, ids, 100, &n); }
static void pg_locus_unknown(void) { const uint16_t ids[1] = {3}; int8_t loci[1]; pg_locus(pg_handle, ids, 1, loci); }

/* PSN offline: matchmaking start-up and the PSN manager's sign-in state. */
typedef int32_t (*f_np_state)(int32_t, uint32_t *);
static f_np_state np_state;
static void np_state_other_user(void) { uint32_t st; np_state(7, &st); }

/* libSceNet epoll sets, offline: waits end only by timeout, abort or destroy. */
typedef int32_t (*f_ep_create)(const char *, int32_t);
typedef int32_t (*f_ep_wait)(int32_t, void *, int32_t, int32_t);
typedef int32_t (*f_ep_id_flags)(int32_t, int32_t);
typedef int32_t (*f_ep_id)(int32_t);
typedef int32_t *(*f_net_errno)(void);
static f_ep_wait ep_wait;
static f_net_errno net_errno_fn;
static int32_t ep_waiter_id, ep_waiter_result, ep_waiter_errno;
static void *ep_waiter(void *arg)
{
    unsigned char events[24 * 4];
    ep_waiter_result = ep_wait(ep_waiter_id, events, 4, (int32_t)(intptr_t)arg);
    ep_waiter_errno = *net_errno_fn();
    return NULL;
}

/* scePthreadExit: a guest thread ends from deep in its calls, its key destructors still run. */
typedef void (*f_thread_exit)(void *);
static f_thread_exit te_exit;
static int32_t te_key;
static atomic_int te_after_exit, te_destructor_calls;
static void *te_destroyed;
static void te_destructor(void *value) { te_destroyed = value; atomic_fetch_add(&te_destructor_calls, 1); }
static void __attribute__((noinline)) te_deep(int depth, void *value)
{
    if (depth) te_deep(depth - 1, value);
    else te_exit(value);
    atomic_store(&te_after_exit, 1); /* never reached */
}
static void *te_entry(void *argument)
{
    kc_set(te_key, argument);
    te_deep(2, argument);
    atomic_store(&te_after_exit, 1); /* never reached */
    return NULL;
}
static void te_exit_main(void) { te_exit(NULL); }

/* libSceSaveData against a temporary save folder. */
typedef struct { int32_t user; uint32_t pad; const char *title, *dir; const void *fp; uint64_t blocks; uint32_t mode; uint8_t r[36]; } sd_mount_req;
typedef struct { char point[16]; uint64_t required; uint32_t unused, status; uint8_t r[32]; } sd_mount_res;
typedef struct { char title[128], subtitle[128], detail[1024]; uint32_t user_param, pad; int64_t mtime; uint8_t r[32]; } sd_param;
typedef struct { int32_t user; uint32_t pad; const char *title, *dir; uint32_t key, order; uint8_t r[32]; } sd_cond;
typedef struct { uint32_t hits, pad; char (*names)[32]; uint32_t capacity, set; sd_param *params; void *infos; uint8_t r[16]; } sd_result;
typedef int32_t (*f_sd_mount)(const sd_mount_req *, sd_mount_res *);
typedef int32_t (*f_sd_point)(const char *);
typedef int32_t (*f_sd_set_param)(const char *, uint32_t, const void *, size_t);
typedef int32_t (*f_sd_search)(const sd_cond *, sd_result *);
typedef int32_t (*f_sd_setup)(int32_t, uint64_t, const sd_param *);
typedef int32_t (*f_sd_memory)(int32_t, void *, uint64_t, int64_t);
static f_sd_mount sd_mount;
static f_sd_memory sd_get;
static void sd_mount_other_title(void) { sd_mount_req q = {.user = 1, .title = "CUSA00000", .dir = "X", .mode = 4}; sd_mount_res r; sd_mount(&q, &r); }
static void sd_mount_other_user(void) { sd_mount_req q = {.user = 9, .dir = "X", .mode = 4}; sd_mount_res r; sd_mount(&q, &r); }
static void sd_get_past_end(void) { char b[8]; sd_get(1, b, 8, 4093); }
static int32_t sd_mount_dir(const char *dir, uint32_t mode, char point[16]);
static int32_t sd_small_stack_result = -1;
static void *sd_small_stack_mount(void *unused)
{
    (void)unused;
    char point[16];
    sd_small_stack_result = sd_mount_dir("SMALLSTACK", 4 | 2, point);
    if (!sd_small_stack_result) sd_small_stack_result = ((f_sd_point)fn("libSceSaveData", "sceSaveDataUmount"))(point);
    return NULL;
}
static int32_t sd_mount_dir(const char *dir, uint32_t mode, char point[16])
{
    sd_mount_req q = {.user = 1, .dir = dir, .blocks = 96, .mode = mode};
    sd_mount_res r;
    memset(&r, 0, sizeof r);
    const int32_t e = sd_mount(&q, &r);
    if (point) memcpy(point, r.point, 16);
    return e;
}
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
    const f1 punlock = (f1)fn("libScePosix", "pthread_mutex_unlock");
    const f1 kplock = (f1)fn("libkernel", "pthread_mutex_lock");
    const f1 kpunlock = (f1)fn("libkernel", "pthread_mutex_unlock");
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

    /* IPMI imports the POSIX names from libkernel. Both namespaces operate on the same
       owned mutex and return BSD errno values, including when SCE names created it. */
    void *cross = NULL;
    CHECK(kpunlock(&cross) == 1 && cross == NULL);
    CHECK(kplock(&cross) == 0 && cross);
    CHECK(plock(&cross) == 11);
    CHECK(punlock(&cross) == 0);
    CHECK(lock(&cross) == 0);
    CHECK(kplock(&cross) == 11);
    CHECK(kpunlock(&cross) == 0);
    CHECK(kpunlock(&cross) == 1);
    CHECK(destroy(&cross) == 0 && cross == (void *)2);
    CHECK(kplock(&cross) == 22 && kpunlock(&cross) == 22);
    CHECK(kplock(NULL) == 22 && kpunlock(NULL) == 22);

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

    /* Every non-recursive type (1 error-checking, 3 normal, 4 adaptive): unlocking or waiting
       without owning the mutex is EPERM, whether it is free or another thread holds it, and the
       owner's trylock is EBUSY (libthr mutex_unlock_common, _mutex_owned, mutex_self_trylock). */
    owner_init = init;
    owner_lock = lock;
    owner_posix_lock = plock;
    owner_trylock = trylock;
    owner_unlock = unlock;
    owner_ainit = ainit;
    owner_settype = settype;
    owner_cond_wait = (f_cond_wait)fn("libkernel", "scePthreadCondWait");
    const finit owner_cond_init = (finit)fn("libkernel", "scePthreadCondInit");
    const f1 owner_cond_destroy = (f1)fn("libkernel", "scePthreadCondDestroy");
    if (failures) return 1;
    alarm(60); /* a mutex that never comes free kills the test with SIGALRM instead of hanging it */
    CHECK(owner_cond_init(&owner_cond, NULL, NULL) == 0);
    const int owner_types[] = {1, 3, 4};
    for (int i = 0; i < 3; i++) {
        void *owner_attr = NULL;
        CHECK(ainit(&owner_attr) == 0 && settype(&owner_attr, owner_types[i]) == 0);
        CHECK(init(&owner_mutex, &owner_attr, NULL) == 0 && adestroy(&owner_attr) == 0);
        CHECK(unlock(&owner_mutex) == (int)0x80020001);                          /* nobody holds it */
        CHECK(owner_cond_wait(&owner_cond, &owner_mutex) == (int)0x80020001);
        CHECK(lock(&owner_mutex) == 0);
        pthread_t owner_other;
        if (pthread_create(&owner_other, NULL, owner_other_thread, NULL)) { perror("pthread_create"); return 1; }
        pthread_join(owner_other, NULL);
        CHECK(owner_other_trylock == (int)0x80020010);                           /* held by main: EBUSY */
        CHECK(owner_other_unlock == (int)0x80020001 && owner_other_wait == (int)0x80020001);
        CHECK(trylock(&owner_mutex) == (int)0x80020010);                         /* the owner again: EBUSY */
        CHECK(unlock(&owner_mutex) == 0);                                        /* still held by main */
        CHECK(unlock(&owner_mutex) == (int)0x80020001);
        CHECK(destroy(&owner_mutex) == 0);
    }
    CHECK(owner_cond_destroy(&owner_cond) == 0);

    /* The owner locking again: EDEADLK for the adaptive type, as for the default (libthr
       mutex_self_lock), also through its static initializer (1); the normal type stops by name. */
    void *owner_adaptive = (void *)1;
    CHECK(lock(&owner_adaptive) == 0 && owner_adaptive != (void *)1);
    CHECK(lock(&owner_adaptive) == (int)0x8002000b && plock(&owner_adaptive) == 11);
    CHECK(unlock(&owner_adaptive) == 0 && destroy(&owner_adaptive) == 0);
    CHECK(died_in_fatal(lock_normal_twice_sce));
    CHECK(died_in_fatal(lock_normal_twice_posix));
    alarm(0);

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

    /* scePthreadYield yields the same way and answers 0, which is right whether the PS4 version
       returns nothing or an int. */
    const f_check pthread_yield = (f_check)fn("libkernel", "scePthreadYield");
    CHECK(pthread_yield && pthread_yield() == 0 && pthread_yield() == 0);
    CHECK(is_implemented("libkernel", "scePthreadYield"));

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
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlGetInfo")); /* test/syslib/netctl_info.c */
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlGetNatInfo"));
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
    /* A second load succeeds and is counted (the dialogs load their module per open). */
    CHECK(sysmodule_load(STUB_ID) == 0 && rt_process.stub_modules[0].loaded == 2);
    CHECK(sysmodule_unload(STUB_ID) == 0 && rt_process.stub_modules[0].loaded == 1);
    CHECK(sysmodule_is_loaded(STUB_ID) == 0);
    CHECK(sysmodule_unload(STUB_ID) == 0 && !rt_process.stub_modules[0].loaded);
    CHECK(sysmodule_load(STUB_ID) == 0 && sysmodule_is_loaded(STUB_ID) == 0);
    /* A full-width request cannot unload the declared stub with matching low bits. */
    CHECK(died_in_fatal(unload_wide_stub));
    CHECK(sysmodule_is_loaded(STUB_ID) == 0);
    CHECK(sysmodule_unload(STUB_ID) == 0 && !rt_process.stub_modules[0].loaded);

    /* Every answer the PS4 has not established stops the process instead of guessing, and the two
       structure calls stop before writing through a buffer whose layout is not established. */
    CHECK(died_in_fatal(unregister_id_a));             /* an id already removed */
    CHECK(died_in_fatal(unregister_unknown_id));       /* an id never handed back */
    CHECK(died_in_fatal(state_into_null));             /* no error value for a bad pointer */
    CHECK(died_in_fatal(register_null_callback));      /* no answer for a null callback */
    CHECK(died_in_fatal(register_null_id));            /* no answer for a null id slot */
    CHECK(died_in_fatal(info_of_module_one));
    {   /* offline: "not connected", nothing written; the guest then takes its own NAT type */
        int nat[4] = {16, -7, -7, -7};
        CHECK(netctl_nat(nat) == (int)0x80412108u && nat[0] == 16 && nat[1] == -7 && nat[2] == -7 && nat[3] == -7);
    }
    CHECK(died_in_fatal(nat_info_other_size));
    CHECK(died_in_fatal(nat_info_null));
    CHECK(memcmp(netctl_shared, poisoned, sizeof poisoned) == 0);

    /* The store survived all of that, because every fatal above was in a child. */
    CHECK(netctl_unregister(netctl_id_b) == 0);
    CHECK(netctl_check() == 0 && netctl_callbacks_run == 0);

    /* getpagesize answers 16 KiB from both libraries (the maintainer's ruling) */
    int32_t (*posix_getpagesize)(void) = (int32_t (*)(void))fn("libScePosix", "getpagesize");
    int32_t (*kernel_getpagesize)(void) = (int32_t (*)(void))fn("libkernel", "getpagesize");
    CHECK(posix_getpagesize && posix_getpagesize() == 0x4000);
    CHECK(kernel_getpagesize && kernel_getpagesize() == 0x4000);
    CHECK(is_implemented("libScePosix", "getpagesize") && is_implemented("libkernel", "getpagesize"));

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

    /* sleep sleeps at least the seconds asked and returns 0, the unslept seconds. Above INT_MAX
       FreeBSD's libc returns a remainder without a signal and POSIX does not: it stops by name. */
    posix_sleep_entry = (unsigned int (*)(unsigned int))fn("libScePosix", "sleep");
    CHECK(posix_sleep_entry(0) == 0);
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(posix_sleep_entry(1) == 0 && elapsed_us(&start) >= 1000000);
    CHECK(died_in_fatal(sleep_above_int_max));
    CHECK(is_implemented("libScePosix", "sleep"));

    /* nanosleep sleeps at least the time asked and returns 0. The guest gets no signal, so the
       remaining-time variable is never written, and the request may be that same variable. The
       largest valid nanosecond count, 999999999, sleeps. Its errno cases and negative seconds stop
       by name. */
    nanosleep_entry = (int (*)(const nanosleep_timespec *, nanosleep_timespec *))fn("libkernel", "nanosleep");
    nanosleep_timespec nanosleep_asked = {0, 0}, nanosleep_left = {7, 7};
    CHECK(nanosleep_entry(&nanosleep_asked, &nanosleep_left) == 0);
    nanosleep_asked = (nanosleep_timespec){0, 999999999};
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(nanosleep_entry(&nanosleep_asked, &nanosleep_left) == 0 && elapsed_us(&start) >= 999999);
    CHECK(nanosleep_left.seconds == 7 && nanosleep_left.nanoseconds == 7);
    nanosleep_asked = (nanosleep_timespec){1, 50000000};
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(nanosleep_entry(&nanosleep_asked, &nanosleep_asked) == 0 && elapsed_us(&start) >= 1050000);
    CHECK(nanosleep_asked.seconds == 1 && nanosleep_asked.nanoseconds == 50000000);
    CHECK(nanosleep_stops_with(nanosleep_no_request, "no requested time"));
    CHECK(nanosleep_stops_with(nanosleep_negative_nanoseconds, "nanoseconds outside"));
    CHECK(nanosleep_stops_with(nanosleep_one_billion_nanoseconds, "nanoseconds outside"));
    CHECK(nanosleep_stops_with(nanosleep_negative_seconds, "negative seconds"));
    CHECK(is_implemented("libkernel", "nanosleep"));

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
        /* Maps the system places are GPU-addressable: below 2^40, so the GPU's 32-bit base fields
           (the address shifted right by 8) hold them whole. */
        CHECK(gpu_addressable(a) && gpu_addressable(b));
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
        /* No overwrite (0x80) with no address: nothing to overwrite, so the map is made as usual, and it
           is the same memory as the range's other maps (the alias a title makes of its own memory). */
        void *alias = NULL;
        CHECK(dm_map(&alias, DM_PAGE, DM_RW, 0x80, first, DM_PAGE) == 0 && alias && alias != a);
        if (alias && a) CHECK(((volatile unsigned char *)alias)[0] == 0xa5);
        /* A free, aligned address hint is where the map goes; one in use or off the alignment stops. */
        void *hinted = (void *)0xfd00000000;
        CHECK(dm_map(&hinted, DM_PAGE, DM_RW, 0, first, DM_PAGE) == 0 && hinted == (void *)0xfd00000000);
        if (a && hinted == (void *)0xfd00000000) CHECK(((volatile unsigned char *)hinted)[0] == 0xa5);
        dm_in_use = a;
        CHECK(died_in_fatal(dm_map_hint_in_use));
        CHECK(died_in_fatal(dm_map_hint_misaligned));
        /* sceKernelVirtualQuery on a direct map: its start, its offset in the pool, direct. */
        vq_fn = (f_vq)fn("libkernel", "sceKernelVirtualQuery");
        if (vq_fn && hinted == (void *)0xfd00000000) {
            unsigned char info[0x48];
            memset(info, 0xee, sizeof info);
            CHECK(vq_fn((char *)hinted + 0x100, 0, info, sizeof info) == 0);
            uint64_t start, end; int64_t offset; int32_t prot, type; uint32_t bits;
            memcpy(&start, info, 8); memcpy(&end, info + 8, 8); memcpy(&offset, info + 0x10, 8);
            memcpy(&prot, info + 0x18, 4); memcpy(&type, info + 0x1c, 4); memcpy(&bits, info + 0x20, 4);
            CHECK(start == 0xfd00000000 && end == start + DM_PAGE && offset == first && prot == DM_RW && type == 0);
            CHECK((bits & 0x2) && !(bits & 0x1) && (bits & 0x10) && info[0x24] == 0);
            /* A map deeper in the pool reports its own offset there. */
            void *deep = (void *)0xfd00100000;
            CHECK(dm_map(&deep, DM_PAGE, DM_RW, 0, windowed, DM_PAGE) == 0 && deep == (void *)0xfd00100000);
            CHECK(vq_fn(deep, 0, info, sizeof info) == 0 && (memcpy(&offset, info + 0x10, 8), offset == windowed));
            CHECK(windowed != 0);
            CHECK(died_in_fatal(vq_flags));
            CHECK(died_in_fatal(vq_size));
        }
        CHECK(died_in_fatal(dm_map_exec));
        /* GPU access (0x10 read, 0x20 write) is accepted; the CPU bits make the host mapping. A
           GPU-visible map the system places goes at the GPU memory base plus its pool offset
           (runtime/gpu_memory.h), and is the same memory as the range's other maps. */
        void *gpu = NULL;
        CHECK(dm_map(&gpu, DM_PAGE, DM_RW | 0x30, 0, dm_unmapped, DM_PAGE) == 0 && gpu);
        CHECK((uintptr_t)gpu == rt_gpu_memory_base() + (uint64_t)dm_unmapped);
        if (gpu) ((volatile unsigned char *)gpu)[0] = 1;
        if (gpu && hinted == (void *)0xfd00000000) {
            void *deep_again = (void *)0xfd00100000; /* the CPU map of the same range, above */
            CHECK(((volatile unsigned char *)deep_again)[0] == 1);
        }
        CHECK(died_in_fatal(dm_map_gpu_twice));
        /* The pool's file, for the renderer's own mapping of the whole pool. */
        int pool_file = -1;
        uint64_t pool_size = 0;
        CHECK(rt_gpu_memory_pool(&pool_file, &pool_size) == 0 && pool_file >= 0 && pool_size == pool);
        /* A GPU-visible map with an address hint keeps the hint, as a CPU one does. */
        int64_t hinted_gpu_range = -1;
        CHECK(dm_allocate(0, (int64_t)pool, DM_PAGE, DM_PAGE, 0, &hinted_gpu_range) == 0);
        void *hinted_gpu = (void *)0xfd00200000;
        CHECK(dm_map(&hinted_gpu, DM_PAGE, DM_RW | 0x30, 0, hinted_gpu_range, DM_PAGE) == 0 && hinted_gpu == (void *)0xfd00200000);
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
        /* The answers that are not established stop by name. */
        CHECK(died_in_fatal(sema_create_no_variable));
        CHECK(died_in_fatal(sema_create_bad_attr));
        CHECK(died_in_fatal(sema_create_option));
        CHECK(died_in_fatal(sema_create_initial_over_max));
        CHECK(died_in_fatal(sema_wait_with_timeout));
        CHECK(died_in_fatal(sema_wait_null));
        CHECK(died_in_fatal(sema_signal_past_max));
        CHECK(died_in_fatal(sema_poll_empty));
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
        typedef int32_t (*f_ac_addcont)(uint32_t, void *, uint32_t, uint32_t *);
        const f_ac_addcont ac_addcont = (f_ac_addcont)fn("libSceAppContent", "sceAppContentGetAddcontInfoList");
        uint32_t hits = 77;
        CHECK(ac_addcont && ac_addcont(0, NULL, 0, &hits) == (int32_t)0x80D90001u); /* before initialize */
        CHECK(ac_init(init, boot) == 0);
        CHECK(boot[0] == 0x5a && boot[sizeof boot - 1] == 0x5a); /* not written */
        int32_t value = -7;
        CHECK(ac_param(1, &value) == 0 && value == 13);
        value = -7;
        CHECK(ac_param(0, &value) == 0 && value == -7);
        CHECK(died_in_fatal(ac_undeclared_param));
        CHECK(died_in_fatal(ac_param_no_variable));
        CHECK(died_in_fatal(ac_init_boot_param_not_declared));
        /* add-ons: the labels the person owns, each installed, under service label 0 */
        if (ac_addcont) {
            CHECK(ac_addcont(0, NULL, 0, &hits) == 0 && hits == 0);              /* none by default */
            memcpy(rt_process.addconts[0], "SPEXPANSIONDLC03", 17);
            memcpy(rt_process.addconts[1], "SECONDADDON", 12);
            rt_process.addcont_count = 2;
            CHECK(ac_addcont(0, NULL, 0, &hits) == 0 && hits == 2);
            CHECK(ac_addcont(1, NULL, 0, &hits) == 0 && hits == 0);
            CHECK(ac_addcont(8, NULL, 0, &hits) == (int32_t)0x80D90002u && ac_addcont(0, NULL, 0, NULL) == (int32_t)0x80D90002u);
            struct { char label[17], padding[3]; uint32_t status; } list[3];
            memset(list, 0x5a, sizeof list);
            CHECK(ac_addcont(0, list, 1, &hits) == 0 && hits == 1);             /* only the space given */
            CHECK(!strcmp(list[0].label, "SPEXPANSIONDLC03") && list[0].status == 4 && list[1].status == 0x5a5a5a5au);
            CHECK(ac_addcont(0, list, 3, &hits) == 0 && hits == 2 && !strcmp(list[1].label, "SECONDADDON"));
            rt_process.addcont_count = 0;
        }
        rt_process.app_param_count = 0;
        rt_process.app_boot_param_unread = 0;
    }
    CHECK(is_implemented("libSceAppContent", "sceAppContentInitialize") &&
          is_implemented("libSceAppContent", "sceAppContentAppParamGetInt"));
    /* libScePosix pthread_cond_*: errno values. */
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

        /* pthread_cond_timedwait on the same objects: ETIMEDOUT (60) once the wall clock reaches
           the time, with the mutex taken back (its owner can unlock it); EINVAL (22) before the
           wait for no time, negative seconds or nanoseconds outside [0, 1000000000), the mutex
           still held; 999999999 nanoseconds and second 0 are valid times; EPERM (1) for a thread
           that does not own the mutex; 0 when signalled in time. */
        pc_timedwait = (int (*)(void **, void **, const pc_timespec *))fn("libScePosix", "pthread_cond_timedwait");
        CHECK(pc_timedwait != NULL);
        if (pc_timedwait) {
            CHECK(pc_lock(&pc_mutex) == 0);
            struct timespec pc_start;
            clock_gettime(CLOCK_MONOTONIC, &pc_start);
            const pc_timespec pc_past = pc_wall_in_ms(-1000);
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_past) == 60);
            CHECK(pc_unlock(&pc_mutex) == 0 && pc_lock(&pc_mutex) == 0);
            const pc_timespec pc_epoch = {0, 0}, pc_last_nanosecond = {pc_past.seconds, 999999999};
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_epoch) == 60);
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_last_nanosecond) == 60);
            CHECK(elapsed_us(&pc_start) < 1500000); /* a passed time does not wait (generous bound) */
            clock_gettime(CLOCK_MONOTONIC, &pc_start);
            const pc_timespec pc_soon = pc_wall_in_ms(50);
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_soon) == 60);
            CHECK(pc_wall_reached(pc_soon) && elapsed_us(&pc_start) >= 40000);
            const pc_timespec pc_negative_ns = {pc_soon.seconds, -1}, pc_billion_ns = {pc_soon.seconds, 1000000000},
                              pc_negative_s = {-1, 0};
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, NULL) == 22);
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_negative_ns) == 22);
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_billion_ns) == 22);
            CHECK(pc_timedwait(&pc_cond, &pc_mutex, &pc_negative_s) == 22);
            pthread_t pc_other;
            pthread_create(&pc_other, NULL, pc_other_waiter, NULL);
            pthread_join(pc_other, NULL);
            CHECK(pc_other_result == 1);
            CHECK(pc_unlock(&pc_mutex) == 0);
            pthread_t pc_timed;
            pthread_create(&pc_timed, NULL, pc_timed_waiter, NULL);
            for (int i = 0; i < 200; i++) sched_yield();
            CHECK(pc_lock(&pc_mutex) == 0);
            pc_timed_go = 1;
            CHECK(pc_signal(&pc_cond) == 0);
            CHECK(pc_unlock(&pc_mutex) == 0);
            pthread_join(pc_timed, NULL);
            CHECK(pc_timed_result == 0);
            CHECK(pc_destroy(&pc_cond) == 0); /* no wait left counted */
        }
    }
    CHECK(is_implemented("libScePosix", "pthread_cond_init") && is_implemented("libScePosix", "pthread_cond_wait") &&
          is_implemented("libScePosix", "pthread_cond_signal") && is_implemented("libScePosix", "pthread_cond_broadcast") &&
          is_implemented("libScePosix", "pthread_cond_destroy") && is_implemented("libScePosix", "pthread_cond_timedwait"));

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
    /* pthread_create_name_np is the same creation under its POSIX name: registered, and the same
       stop for a missing attribute. */
    tc_create_posix = (f_create)fn("libScePosix", "pthread_create_name_np");
    CHECK(tc_create_posix != NULL && is_implemented("libScePosix", "pthread_create_name_np"));
    if (tc_create_posix) CHECK(died_in_fatal(posix_create_without_attr));
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
        CHECK(tc_affinity(NULL, 0x30) == (int)0x80020003u); /* no thread: ESRCH, as libthr */
    }

    /* scePthreadKey*: keys are slot + 1, values belong to each thread, a deleted key reads NULL
       and its slot's next key does not see old values; at a created thread's end each value is
       cleared and its live key's destructor called, in rounds (FreeBSD libthr thr_spec.c). */
    kc_create = (f_key_create)fn("libkernel", "scePthreadKeyCreate");
    kc_delete = (f_key)fn("libkernel", "scePthreadKeyDelete");
    kc_get = (f_key_get)fn("libkernel", "scePthreadGetspecific");
    kc_set = (f_key_set)fn("libkernel", "scePthreadSetspecific");
    if (kc_create && kc_delete && kc_get && kc_set && tc_create) {
        int main_value = 0;
        int32_t gone = 0, reused = 0;
        CHECK(kc_create(&kc_key, kc_destructor) == 0 && kc_key >= 1);
        CHECK(kc_create(&gone, NULL) == 0 && gone >= 1 && gone != kc_key);
        CHECK(kc_get(gone) == NULL); /* never set by this thread */
        CHECK(kc_set(gone, &main_value) == 0 && kc_get(gone) == &main_value);
        CHECK(kc_delete(gone) == 0 && kc_get(gone) == NULL);
        CHECK(kc_delete(gone) == (int)0x80020016 && kc_set(gone, &main_value) == (int)0x80020016);
        CHECK(kc_create(&reused, NULL) == 0 && reused == gone && kc_get(reused) == NULL);
        CHECK(kc_delete(0) == (int)0x80020016 && kc_set(0, &main_value) == (int)0x80020016 && kc_get(0) == NULL);
        CHECK(kc_delete(1000) == (int)0x80020016 && kc_get(1000) == NULL);
        CHECK(kc_create(NULL, NULL) == (int)0x80020016);
        kc_plain_key = reused;
        CHECK(kc_create(&kc_deleted_key, kc_deleted_destructor) == 0);
        CHECK(kc_set(kc_key, &main_value) == 0);
        void *worker = NULL, *attr = tc_attr(1, 1, 1);
        CHECK(tc_create(&worker, &attr, kc_entry, &kc_marker, "keys") == 0);
        for (int i = 0; i < 100000 && !atomic_load(&kc_done); i++) sched_yield();
        struct timespec settle = {0, 50 * 1000 * 1000};
        nanosleep(&settle, NULL); /* room for a wrong third call */
        CHECK(kc_seen_before == NULL); /* the main thread's value is not the worker's */
        CHECK(atomic_load(&kc_calls) == 2 && kc_destroyed[0] == &kc_marker && kc_destroyed[1] == &kc_marker);
        CHECK(kc_seen_in_destructor == NULL); /* cleared before the destructor runs */
        CHECK(atomic_load(&kc_deleted_calls) == 0);
        CHECK(kc_get(kc_key) == &main_value);
        CHECK(died_in_fatal(kc_endless));
        CHECK(kc_exits_cleanly(kc_fill_128)); /* the 128th live key is created */
        CHECK(died_in_fatal(kc_too_many));    /* only the 129th stops */
        CHECK(kc_exits_cleanly(kc_four_rounds)); /* exactly 4 destructor calls, the thread ends cleanly */
        CHECK(died_in_fatal(kc_five_rounds));    /* a value still set after the 4th round stops */
        CHECK(kc_delete(kc_key) == 0 && kc_delete(reused) == 0);
    }
    CHECK(is_implemented("libkernel", "scePthreadKeyCreate") && is_implemented("libkernel", "scePthreadKeyDelete") &&
          is_implemented("libkernel", "scePthreadGetspecific") && is_implemented("libkernel", "scePthreadSetspecific"));

    /* libSceUserService: one user, logged in from the start; a second initialize answers
       0x80960003, as the executable's second site expects. */
    const f_us_init us_init = (f_us_init)fn("libSceUserService", "sceUserServiceInitialize");
    us_terminate = (f_us_none)fn("libSceUserService", "sceUserServiceTerminate");
    us_initial_user = (f_us_ids)fn("libSceUserService", "sceUserServiceGetInitialUser");
    us_login_list = (f_us_ids)fn("libSceUserService", "sceUserServiceGetLoginUserIdList");
    us_name = (f_us_name)fn("libSceUserService", "sceUserServiceGetUserName");
    us_event = (f_us_event)fn("libSceUserService", "sceUserServiceGetEvent");
    CHECK(us_name != NULL && us_event != NULL);
    if (us_init && us_terminate && us_initial_user && us_login_list && us_name && us_event) {
        CHECK(died_in_fatal(us_terminate_uninitialized));
        CHECK(died_in_fatal(us_event_uninitialized));
        CHECK(died_in_fatal(us_name_uninitialized));
        CHECK(died_in_fatal(us_initial_user_uninitialized));
        const uint32_t params[4] = {0x2bc, 0, 0, 0};
        CHECK(us_init(params) == 0);
        CHECK(us_init(params) == (int32_t)0x80960003u);
        int32_t user = -7, list[4] = {-7, -7, -7, -7};
        CHECK(us_initial_user(&user) == 0 && user != -1 && user != 0xff && user != -7);
        CHECK(us_login_list(list) == 0 && list[0] == user && list[1] == -1 && list[2] == -1 && list[3] == -1);
        us_user = user;
        char name[17];
        memset(name, 0x7f, sizeof name);
        CHECK(us_name(user, name, sizeof name) == 0 && strcmp(name, "Hunter") == 0 && name[7] == 0x7f);
        char exact[7];
        CHECK(us_name(user, exact, sizeof exact) == 0 && strcmp(exact, "Hunter") == 0); /* the name and its end */
        CHECK(died_in_fatal(us_name_other_user));
        CHECK(died_in_fatal(us_name_short));
        CHECK(died_in_fatal(us_name_no_buffer));
        int32_t event[2] = {-7, -7};
        CHECK(us_event(event) == (int32_t)0x80960007u && event[0] == -7 && event[1] == -7); /* no event, nothing written */
        CHECK(died_in_fatal(us_event_no_variable));
        CHECK(us_terminate() == 0);
        CHECK(us_init(params) == 0);    /* initialized again after a terminate */
        CHECK(us_terminate() == 0);
    }

    pad_init = (f_pad_none)fn("libScePad", "scePadInit");
    pad_open = (f_pad_open)fn("libScePad", "scePadOpen");
    pad_close = (f_pad_handle)fn("libScePad", "scePadClose");
    pad_read = (f_pad_block)fn("libScePad", "scePadReadState");
    pad_info = (f_pad_block)fn("libScePad", "scePadGetControllerInformation");
    pad_vibrate = (f_pad_vibrate)fn("libScePad", "scePadSetVibration");
    CHECK(pad_init && pad_open && pad_close && pad_read && pad_info && pad_vibrate);
    if (pad_init && pad_open && pad_close && pad_read && pad_info && pad_vibrate) {
        CHECK(died_in_fatal(pad_open_before_init));
        CHECK(pad_init() == 0);
        CHECK(died_in_fatal(pad_init_again));
        CHECK(died_in_fatal(pad_open_other_user));
        CHECK(died_in_fatal(pad_open_special));
        CHECK(died_in_fatal(pad_open_index));
        pad_handle = pad_open(1, 0, 0, NULL);
        CHECK(pad_handle >= 0);
        CHECK(pad_open(1, 0, 0, NULL) == (int32_t)0x80920004u); /* already opened */
        unsigned char state[128];
        memset(state, 0xee, sizeof state);
        CHECK(pad_read(pad_handle, state) == 0);
        uint32_t buttons;
        float w, ax;
        memcpy(&buttons, state, 4);
        memcpy(&w, state + 24, 4);
        memcpy(&ax, state + 28, 4);
        CHECK(buttons == 0 && state[4] == 0x80 && state[5] == 0x80 && state[6] == 0x80 && state[7] == 0x80);
        CHECK(state[8] == 0 && state[9] == 0 && w == 1.0f && ax == 0.0f);
        CHECK(state[0x4c] == 1 && state[119] == 0 && state[120] == 0xee); /* connected; nothing past 120 bytes */
        unsigned char info[32];
        memset(info, 0xee, sizeof info);
        CHECK(pad_info(pad_handle, info) == 0);
        uint16_t width, height;
        int32_t connected, device_class;
        memcpy(&width, info + 4, 2);
        memcpy(&height, info + 6, 2);
        memcpy(&connected, info + 12, 4);
        memcpy(&device_class, info + 16, 4);
        CHECK(width == 1920 && height == 942 && info[8] > 0 && info[8] < 0x80 && info[9] == info[8]);
        CHECK(info[10] == 0 && connected == 1 && device_class == 0 && info[27] == 0 && info[28] == 0xee);
        CHECK(died_in_fatal(pad_read_bad_handle));
        CHECK(died_in_fatal(pad_read_no_block));
        CHECK(died_in_fatal(pad_info_no_block));
        CHECK(rt_pad_vibration() == 0);
        const uint8_t speeds[2] = {0xc0, 0x40};
        CHECK(pad_vibrate(pad_handle, speeds) == 0 && rt_pad_vibration() == 0x40c0);
        const uint8_t stop[2] = {0, 0};
        CHECK(pad_vibrate(pad_handle, stop) == 0 && rt_pad_vibration() == 0);
        CHECK(died_in_fatal(pad_vibrate_missing));
        CHECK(pad_close(pad_handle) == 0);
        CHECK(died_in_fatal(pad_read_closed));
        CHECK(died_in_fatal(pad_vibrate_closed));
        CHECK(pad_open(1, 0, 0, NULL) == pad_handle); /* opened again after a close */
        CHECK(pad_close(pad_handle) == 0);
    }

    /* libSceCommonDialog: the first initialize succeeds, a repeat answers 0x80b80002 as the
       executable's second site expects. */
    const f_us_none cd_init = (f_us_none)fn("libSceCommonDialog", "sceCommonDialogInitialize");
    if (cd_init) {
        CHECK(cd_init() == 0);
        CHECK(cd_init() == (int32_t)0x80b80002u);
    }

    /* libSceSystemService: the language the person running the runtime chose. */
    {   /* the boot splash: hidden when the title says so */
        typedef int32_t (*f_hide)(void);
        const f_hide hide = (f_hide)fn("libSceSystemService", "sceSystemServiceHideSplashScreen");
        CHECK(hide && !rt_splash_hidden() && hide() == 0 && rt_splash_hidden() && hide() == 0);
        typedef int32_t (*f_status)(void *);
        const f_status status_fn = (f_status)fn("libSceSystemService", "sceSystemServiceGetStatus");
        uint8_t status[16];
        memset(status, 0x5a, sizeof status);
        CHECK(status_fn && status_fn(status) == 0 && !status[0] && !status[3] && !status[4] && !status[5] && status[6] == 1);
        CHECK(!status[7] && !status[8] && status[9] == 0x5a);                      /* nothing past 9 bytes */
        CHECK(status_fn(NULL) == (int32_t)0x80A10003u);
    }
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
        CHECK(gpu_addressable(a) && gpu_addressable(b) && (uintptr_t)a % 0x4000 == 0);
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
            typedef int32_t (*f_vq2)(const void *, int32_t, void *, uint64_t);
            const f_vq2 vq = (f_vq2)fn("libkernel", "sceKernelVirtualQuery");
            if (vq) { /* a flexible map: the flexible bit, no offset */
                unsigned char info[0x48]; uint32_t bits; int64_t off; uint64_t start;
                CHECK(vq((char *)a + 0x10, 0, info, sizeof info) == 0);
                memcpy(&bits, info + 0x20, 4); memcpy(&off, info + 0x10, 8); memcpy(&start, info, 8);
                CHECK((bits & 0x1) && !(bits & 0x2) && off == 0 && start == (uint64_t)(uintptr_t)a);
            }
            qmp_fn = qmp;
            CHECK(died_in_fatal(qmp_unmapped));
            /* the mapping's start, and its end one past the last byte (shadPS4, PS4Recomp) */
            void *start = NULL, *end = NULL;
            CHECK(qmp((char *)a + 0x10, &start, &end, &prot) == 0 && prot == 3);
            CHECK(start == a && end == (char *)a + 0x8000);
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
                /* HTTPS flags on a template: the default, enabling and disabling, and the errors. */
                typedef int32_t (*f_https_option)(int32_t, uint32_t);
                const f_https_option enable = (f_https_option)fn("libSceHttp", "sceHttpsEnableOption");
                const f_https_option disable = (f_https_option)fn("libSceHttp", "sceHttpsDisableOption");
                CHECK(enable && disable);
                if (enable && disable) {
                    CHECK(rt_http_template_ssl_flags(t1) == 0xa5); /* server verify, CN and known-CA checks, SNI */
                    CHECK(disable(t1, 1) == 0 && rt_http_template_ssl_flags(t1) == 0xa4);
                    CHECK(enable(t1, 1) == 0 && rt_http_template_ssl_flags(t1) == 0xa5);
                    CHECK(enable(t1, 0x2000 | 0x40) == 0 && rt_http_template_ssl_flags(t1) == 0x20e5);
                    CHECK(disable(t1, 0x2000 | 0x40 | 0x80) == 0 && rt_http_template_ssl_flags(t1) == 0x25);
                    CHECK(enable(t1, 0x100) == (int32_t)0x804311feu && rt_http_template_ssl_flags(t1) == 0x25);
                    CHECK(disable(t1, 0x4000) == (int32_t)0x804311feu);
                    CHECK(enable(999, 1) == (int32_t)0x80431100u && disable(0, 1) == (int32_t)0x80431100u);
                    CHECK(rt_http_template_ssl_flags(999) == 0);
                }
                http_epoll_fn = (f_http_epoll)fn("libSceHttp", "sceHttpCreateEpoll");
                if (http_epoll_fn) {
                    void *h1 = NULL, *h2 = NULL;
                    CHECK(http_epoll_fn(http, &h1) == 0 && h1);
                    CHECK(http_epoll_fn(http, &h2) == 0 && h2 && h2 != h1);
                    CHECK(died_in_fatal(http_epoll_unknown));
                    CHECK(died_in_fatal(http_epoll_no_slot));
                    /* sceHttpWaitRequest: offline, a poll of a handle the runtime made finds no events. */
                    http_wait_fn = (f_http_wait)fn("libSceHttp", "sceHttpWaitRequest");
                    CHECK(http_wait_fn != NULL);
                    if (http_wait_fn) {
                        static unsigned char events[128][24];
                        memset(events, 0xa5, sizeof events);
                        http_wait_handle = h1;
                        CHECK(http_wait_fn(h1, events, 128, 0) == 0 && events[0][0] == 0xa5);
                        CHECK(died_in_fatal(http_wait_unknown));
                        CHECK(died_in_fatal(http_wait_timeout));
                    }
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

    /* time (libSceLibcInternal): the wall clock's whole seconds, returned and stored through a
       non-null pointer, as FreeBSD's time() reads CLOCK_SECOND. */
    int64_t (*libc_time)(int64_t *) = (int64_t (*)(int64_t *))fn("libSceLibcInternal", "time");
    if (libc_time) {
        struct timespec before, after;
        int64_t stored = -1;
        clock_gettime(CLOCK_REALTIME, &before);
        const int64_t returned = libc_time(&stored);
        const int64_t unstored = libc_time(NULL);
        clock_gettime(CLOCK_REALTIME, &after);
        CHECK(returned == stored && returned >= before.tv_sec && returned <= after.tv_sec);
        CHECK(unstored >= returned && unstored <= after.tv_sec);
    }
    CHECK(is_implemented("libSceLibcInternal", "time"));

    /* libSceAjm */
    {
        const f_ajm_init init = (f_ajm_init)fn("libSceAjm", "sceAjmInitialize");
        const f_ajm_register reg = (f_ajm_register)fn("libSceAjm", "sceAjmModuleRegister");
        const f_ajm_create create = (f_ajm_create)fn("libSceAjm", "sceAjmInstanceCreate");
        const f_ajm_two destroy = (f_ajm_two)fn("libSceAjm", "sceAjmInstanceDestroy");
        const f_ajm_two cancel = (f_ajm_two)fn("libSceAjm", "sceAjmBatchCancel");
        const f_ajm_control control = (f_ajm_control)fn("libSceAjm", "sceAjmBatchJobControlBufferRa");
        const f_ajm_wait wait = (f_ajm_wait)fn("libSceAjm", "sceAjmBatchWait");
        typedef int32_t (*f_ajm_one)(uint32_t);
        const f_ajm_one finalize = (f_ajm_one)fn("libSceAjm", "sceAjmFinalize");
        ajm_run_fn = (f_ajm_run)fn("libSceAjm", "sceAjmBatchJobRunBufferRa");
        ajm_start_fn = (f_ajm_start)fn("libSceAjm", "sceAjmBatchStartBuffer");
        CHECK(init && reg && create && destroy && cancel && control && wait && finalize && ajm_run_fn && ajm_start_fn);
        if (init && reg && create && destroy && cancel && control && wait && finalize && ajm_run_fn && ajm_start_fn) {
            uint32_t context = 0;
            CHECK(init(1, &context) == (int32_t)0x80930005u && init(0, NULL) == (int32_t)0x80930005u);
            CHECK(init(0, &context) == 0 && context);
            ajm_test_context = context;
            CHECK(reg(99, 1, 0) == (int32_t)0x80930002u && reg(context, 23, 0) == (int32_t)0x80930005u);
            CHECK(reg(context, 1, 0) == 0 && reg(context, 1, 0) == (int32_t)0x80930009u);
            uint32_t instance = 0;
            CHECK(create(context, 1, 0x410, &instance) == (int32_t)0x8093000Bu);  /* revision 0 */
            CHECK(create(context, 0, 0x411, &instance) == (int32_t)0x8093000Au);  /* MP3 not registered */
            CHECK(create(context, 1, 0x411, &instance) == 0 && instance >> 14 == 1 && (instance & 0x3fff) >= 1);
            ajm_test_instance = instance;
            /* job sizes the title's FMOD relies on: control 0x30 (0x40 with a return address), run 0x40 (0x50) */
            static uint8_t batch[0x100];
            static uint8_t init_params[8] = {0xFE, 0x72, 0x1F, 0xF0}; /* 48 kHz, stereo, 256-byte frames, 4 per superframe */
            static uint8_t control_out[32], run_out[64];
            uint8_t *at = control(batch, instance, 0x6001, init_params, sizeof init_params, control_out, sizeof control_out, NULL);
            CHECK(at - batch == 0x30);
            uint8_t *with_ra = control(batch + 0x80, instance, 0x6001, init_params, 8, control_out, 32, (void *)0x1234);
            CHECK(with_ra - (batch + 0x80) == 0x40);
            uint8_t *run_end = ajm_run_fn(at, instance, 0x801, NULL, 0, NULL, 0, run_out, sizeof run_out, (void *)0x1234);
            CHECK(run_end - at == 0x50);
            uint32_t batch_id = 0;
            CHECK(ajm_start_fn(context, batch, 4, 0, NULL, &batch_id) == (int32_t)0x80930011u);
            memset(control_out, 0xee, sizeof control_out);
            memset(run_out, 0xee, sizeof run_out);
            CHECK(ajm_start_fn(context, batch, (uint32_t)(run_end - batch), 0, NULL, &batch_id) == 0 && batch_id >= 1);
            int32_t result[2];
            memcpy(result, control_out, 8);
            CHECK(result[0] == 0 && result[1] == 0);
            uint32_t info[6];
            memcpy(info, run_out, sizeof info);
            CHECK(info[0] == 0 && info[1] == 0);                                  /* the run's result */
            CHECK(info[2] == 1024 && info[3] == 4 && info[4] == 1024 && info[5] == 256); /* the ATRAC9 codec info */
            CHECK(cancel(context, batch_id) == 0);                                /* already run: nothing to cancel */
            CHECK(wait(context, batch_id, 0xffffffffu, NULL) == 0 && wait(context, batch_id, 0, NULL) == (int32_t)0x80930004u);
            CHECK(cancel(context, batch_id) == (int32_t)0x80930004u);
            /* decoding, with the stand-in decoder: frames, room, partial input, superframe padding, gapless */
            static const rt_ajm_decoder fake = {ajm_fake_open, ajm_fake_decode, ajm_fake_release};
            rt_ajm_set_decoder(&fake);
            static uint8_t at9[4096];
            static int16_t pcm[256 * 2 * 8];
            struct { int32_t result, internal; int32_t consumed, written; uint64_t total; uint32_t frames, reserved; } side;
            const uint64_t stream_frames = 0x800000001000ull;                     /* Stream + MultipleFrames */
            CHECK(control(batch, instance, 0x6001, init_params, 8, control_out, 32, NULL) != NULL);
            uint32_t init_id;
            const uint8_t *init_end = control(batch, instance, 0x6001, init_params, 8, control_out, 32, NULL);
            CHECK(ajm_start_fn(context, batch, (uint32_t)(init_end - batch), 0, NULL, &init_id) == 0 && wait(context, init_id, 0, NULL) == 0);
            ajm_fake_next = 0;
            memset(&side, 0, sizeof side);
            CHECK(ajm_job(stream_frames, at9, 1024, pcm, sizeof pcm, &side, sizeof side) == 0);
            CHECK(side.result == 0 && side.consumed == 1024 && side.written == 4 * 1024 && side.total == 1024 && side.frames == 4);
            CHECK(pcm[0] == 0 && pcm[511] == 511 && pcm[2047] == 2047);         /* frames in order, interleaved */
            memset(&side, 0, sizeof side);                                         /* room for one frame and a half */
            CHECK(ajm_job(stream_frames, at9, 1024, pcm, 1536, &side, sizeof side) == 0);
            CHECK(side.result == 0x10 && side.frames == 1 && side.consumed == 256 && side.written == 1024);
            memset(&side, 0, sizeof side);                                         /* less than the rest of the superframe */
            CHECK(ajm_job(stream_frames, at9, 512, pcm, sizeof pcm, &side, sizeof side) == 0);
            CHECK(side.result == 0x8 && side.frames == 0 && side.consumed == 0);
            memset(&side, 0, sizeof side);                                         /* the rest: three frames */
            CHECK(ajm_job(stream_frames, at9, 768, pcm, sizeof pcm, &side, sizeof side) == 0 && side.frames == 3 && side.total == 2048u); /* since the reset */
            ajm_fake_frame_bytes = 200;                                            /* frames shorter than a quarter: padding */
            memset(&side, 0, sizeof side);
            CHECK(ajm_job(stream_frames, at9, 1024, pcm, sizeof pcm, &side, sizeof side) == 0 && side.consumed == 1024 && side.frames == 4);
            ajm_fake_frame_bytes = 256;
            /* gapless: skip 100, keep 300 samples; the rest of the superframe is decoded and dropped */
            struct { uint32_t total; uint16_t skip, skipped; } gap = {300, 100, 0};
            uint32_t gap_id;
            const uint8_t *gap_end = control(batch, instance, 0x200000002001ull, &gap, sizeof gap, control_out, 32, NULL);
            CHECK(ajm_start_fn(context, batch, (uint32_t)(gap_end - batch), 0, NULL, &gap_id) == 0 && wait(context, gap_id, 0, NULL) == 0);
            struct { int32_t result, internal; int32_t consumed, written; uint64_t total; uint32_t gap_total; uint16_t gap_skip, gap_skipped; uint32_t frames, r; } gside;
            memset(&gside, 0, sizeof gside);
            ajm_fake_next = 0;
            CHECK(ajm_job(stream_frames | (1ull << 45), at9, 1024, pcm, sizeof pcm, &gside, sizeof gside) == 0);
            CHECK(gside.result == 0 && gside.written == 300 * 4 && gside.total == 300u && gside.consumed == 1024 && gside.frames == 2);
            CHECK(pcm[0] == 200 && gside.gap_total == 0 && gside.gap_skip == 0);   /* 100 stereo samples skipped */
            /* a decoder error is fatal, with the decoder's code */
            ajm_fake_fail = 1;
            memset(&side, 0, sizeof side);
            CHECK(ajm_job(stream_frames, at9, 1024, pcm, sizeof pcm, &side, sizeof side) == 0);
            CHECK((uint32_t)side.result == 0xC0000000u && side.internal == -5);
            ajm_fake_fail = 0;
            rt_ajm_set_decoder(NULL);
            /* the real decoder: junk input is a codec error */
            static uint8_t junk[1024];
            for (int k = 0; k < 1024; k++) junk[k] = (uint8_t)(k * 37 + 11);
            const uint8_t *real_end = control(batch, instance, 0x6001, init_params, 8, control_out, 32, NULL);
            CHECK(ajm_start_fn(context, batch, (uint32_t)(real_end - batch), 0, NULL, &init_id) == 0 && wait(context, init_id, 0, NULL) == 0);
            memset(&side, 0, sizeof side);
            CHECK(ajm_job(stream_frames, junk, sizeof junk, pcm, sizeof pcm, &side, sizeof side) == 0);
            CHECK(((uint32_t)side.result & 0xC0000000u) == 0xC0000000u && side.internal != 0);
            CHECK(destroy(context, instance) == 0 && destroy(context, instance) == (int32_t)0x80930003u);
            CHECK(finalize(context) == 0 && finalize(context) == (int32_t)0x80930002u);
        }
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
            /* no block (NULL): wait for room, hand nothing over, so the timeline does not move on;
               three such calls take about one block (5.33 ms), not three (16 ms), as shadPS4's and
               PS4Recomp's waits for room without queueing */
            clock_gettime(CLOCK_MONOTONIC, &start);
            for (int i = 0; i < 3; i++) CHECK(ao_output(port, NULL) == 0);
            CHECK(elapsed_us(&start) < 10000);
            CHECK(ao_output(port, block) == 0); /* the next block about a block after the last one */
            CHECK(elapsed_us(&start) < 15000);
        }
        /* sceAudioOutGetPortState: nothing is heard, so output (the first 16 bits) is 0. */
        typedef int32_t (*f_ao_state)(int32_t, void *);
        const f_ao_state ao_state = (f_ao_state)fn("libSceAudioOut", "sceAudioOutGetPortState");
        if (ao_state) {
            unsigned char state[32];
            memset(state, 0xee, sizeof state);
            CHECK(ao_state(h1, state) == 0 && state[0] == 0 && state[1] == 0 && state[31] == 0);
            /* host sound: this build has none, so it cannot be switched on, and no port is heard */
            CHECK(rt_audio_host_configure(1) == -1 && !rt_audio_host_enabled() && rt_audio_host_configure(0) == 0);
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
            /* POSIX stat: the same answer, 0 or -1 with the thread's errno (__error) set. */
            pstat_fn = (f_pstat)fn("libScePosix", "stat");
            const f_errno error_fn = (f_errno)fn("libkernel", "__error");
            CHECK(pstat_fn && error_fn);
            if (pstat_fn && error_fn) {
                int *const err = error_fn();
                CHECK(err != NULL && err == error_fn()); /* the same int for one thread */
                pthread_t other;
                CHECK(pthread_create(&other, NULL, errno_of_thread, (void *)error_fn) == 0 && pthread_join(other, NULL) == 0);
                CHECK(other_thread_errno && other_thread_errno != err); /* another thread has its own */
                memset(st, 0xee, sizeof st);
                *err = 77;
                CHECK(pstat_fn("/app0/data.bin", st) == 0 && *err == 77); /* success leaves errno alone */
                memcpy(&mode, st + 8, 2); memcpy(&size, st + 0x48, 8);
                CHECK((mode & 0170000) == 0100000 && (mode & 0777) == 0444 && size == 10);
                CHECK(pstat_fn("/app0/missing.bin", st) == -1 && *err == 2);           /* ENOENT */
                CHECK(pstat_fn("/app0/data.bin/inside", st) == -1 && *err == 20);      /* ENOTDIR */
                CHECK(died_in_fatal(pstat_outside));
                CHECK(died_in_fatal(pstat_no_buffer));
            }
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

                /* /data: the writable scratch mount. The engine's cache open (O_RDWR | O_CREAT |
                   O_TRUNC, mode 0777) creates an empty file there; it shows write permission. */
                static char data_buf[512];
                snprintf(data_buf, sizeof data_buf, "%s/pb-data-XXXXXX", tmp ? tmp : "/tmp");
                char *data = mkdtemp(data_buf);
                CHECK(data != NULL);
                if (data) {
                    rt_process.data_dir = data;
                    char cache[600];
                    snprintf(cache, sizeof cache, "%s/cache.bin", data);
                    int32_t cfd = kopen("/data/cache.bin", 0x602, 0777);
                    CHECK(cfd >= 3 && access(cache, F_OK) == 0);
                    memset(st, 0, sizeof st);
                    CHECK(kfstat(cfd, st) == 0 && (memcpy(&mode, st + 8, 2), (mode & 0200) != 0));
                    CHECK(kread(cfd, buf, 4) == 0 && kclose(cfd) == 0);
                    FILE *c = fopen(cache, "w");
                    if (c) { fwrite("abc", 1, 3, c); fclose(c); }
                    cfd = kopen("/data/cache.bin", 0, 0); /* read-only sees what is there */
                    CHECK(cfd >= 3 && kread(cfd, buf, 4) == 3 && !memcmp(buf, "abc", 3) && kclose(cfd) == 0);
                    CHECK(kstat_fn("/data/cache.bin", st) == 0 && (memcpy(&size, st + 0x48, 8), size == 3));
                    cfd = kopen("/data/cache.bin", 0x602, 0777); /* truncate starts it over */
                    CHECK(cfd >= 3 && kread(cfd, buf, 4) == 0 && kclose(cfd) == 0);
                    CHECK(kopen("/data/missing.bin", 0, 0) == (int32_t)0x80020002u);

                    /* Writing: sceKernelWrite and sceKernelPwrite. Guest open flags: O_WRONLY 1,
                       O_RDWR 2, O_APPEND 0x8, O_CREAT 0x200, O_TRUNC 0x400. */
                    kwrite_fn = (f_kwrite)fn("libkernel", "sceKernelWrite");
                    kpwrite_fn = (f_kpwrite)fn("libkernel", "sceKernelPwrite");
                    CHECK(kwrite_fn && kpwrite_fn);
                    CHECK(is_implemented("libkernel", "sceKernelWrite") && is_implemented("libkernel", "sceKernelPwrite"));
                    if (kwrite_fn && kpwrite_fn) {
                        char written[600];
                        snprintf(written, sizeof written, "%s/written.bin", data);
                        write_fd = kopen("/data/written.bin", 0x602, 0644);
                        CHECK(write_fd >= 3);
                        CHECK(kwrite_fn(write_fd, "hello", 5) == 5);
                        CHECK(kwrite_fn(write_fd, NULL, 0) == 0 && kwrite_fn(write_fd, "x", 0) == 0); /* zero bytes */
                        CHECK(kpwrite_fn(write_fd, "J", 1, 0) == 1);
                        CHECK(klseek(write_fd, 0, 1) == 5); /* the pwrite left the position after "hello" */
                        CHECK(kwrite_fn(write_fd, "!", 1) == 1);
                        CHECK(kpread(write_fd, buf, 16, 0) == 6 && !memcmp(buf, "Jello!", 6));
                        CHECK(kpwrite_fn(write_fd, "z", 1, 8) == 1); /* past the end: a hole of zero bytes */
                        CHECK(kpread(write_fd, buf, 16, 0) == 9 && !memcmp(buf, "Jello!\0\0z", 9));
                        CHECK(kpwrite_fn(write_fd, "q", 1, -1) == (int32_t)0x80020016u); /* EINVAL */
                        CHECK(kpwrite_fn(write_fd, NULL, 0, 0) == 0);
                        CHECK(kfstat(write_fd, st) == 0 && (memcpy(&size, st + 0x48, 8), size == 9));
                        /* EBADF: descriptors that are not open for writing. */
                        const int32_t rfd = kopen("/data/written.bin", 0, 0);
                        CHECK(rfd >= 3 && kwrite_fn(rfd, "w", 1) == (int32_t)0x80020009u);
                        CHECK(kpwrite_fn(rfd, "w", 1, 0) == (int32_t)0x80020009u);
                        CHECK(kpwrite_fn(rfd, "w", 1, -1) == (int32_t)0x80020009u); /* the descriptor before the offset */
                        /* Exactly INT_MAX bytes passes the size check (which comes first), so the
                           descriptor answers; the buffer is never read. */
                        CHECK(kwrite_fn(rfd, "w", 0x7fffffff) == (int32_t)0x80020009u);
                        CHECK(kpwrite_fn(rfd, "w", 0x7fffffff, 0) == (int32_t)0x80020009u);
                        write_read_only_fd = rfd; /* one byte more stops before the descriptor answers */
                        CHECK(write_stops_with(write_too_many_read_only, "sceKernelWrite", "more than INT_MAX"));
                        CHECK(write_stops_with(pwrite_too_many_read_only, "sceKernelPwrite", "more than INT_MAX"));
                        CHECK(kclose(rfd) == 0 && kwrite_fn(rfd, "w", 1) == (int32_t)0x80020009u); /* closed */
                        const int32_t afd = kopen("/app0/data.bin", 0, 0);
                        CHECK(afd >= 3 && kwrite_fn(afd, "w", 1) == (int32_t)0x80020009u);
                        CHECK(kpwrite_fn(afd, "w", 1, 0) == (int32_t)0x80020009u && kclose(afd) == 0);
                        CHECK(kwrite_fn(1, "w", 1) == (int32_t)0x80020009u && kpwrite_fn(2, "w", 1, 0) == (int32_t)0x80020009u);
                        FILE *a = fopen(file, "rb");
                        char app0_bytes[16] = {0};
                        CHECK(a && fread(app0_bytes, 1, sizeof app0_bytes, a) == 10 && !memcmp(app0_bytes, "0123456789", 10));
                        if (a) fclose(a);
                        CHECK(kpread(write_fd, buf, 16, 0) == 9 && !memcmp(buf, "Jello!\0\0z", 9)); /* still unchanged */
                        /* O_WRONLY | O_APPEND: each write goes to the end. */
                        write_append_fd = kopen("/data/written.bin", 0x9, 0);
                        CHECK(write_append_fd >= 3 && kwrite_fn(write_append_fd, "+", 1) == 1);
                        CHECK(kpread(write_fd, buf, 16, 0) == 10 && !memcmp(buf, "Jello!\0\0z+", 10));
                        CHECK(write_stops_with(write_too_many, "sceKernelWrite", "more than INT_MAX"));
                        CHECK(write_stops_with(pwrite_too_many, "sceKernelPwrite", "more than INT_MAX"));
                        CHECK(write_stops_with(write_no_buffer, "sceKernelWrite", "no buffer"));
                        CHECK(write_stops_with(pwrite_no_buffer, "sceKernelPwrite", "no buffer"));
                        CHECK(write_stops_with(pwrite_append, "sceKernelPwrite", "O_APPEND"));
                        CHECK(kclose(write_append_fd) == 0 && kclose(write_fd) == 0);
                        unlink(written);
                        kopen_fn = kopen;
                        CHECK(write_returns_short_count());
                        snprintf(written, sizeof written, "%s/short.bin", data);
                        unlink(written);
                    }
                    kopen_fn = kopen;
                    CHECK(died_in_fatal(kstat_data_dotdot));
                    CHECK(died_in_fatal(data_open_odd_flag));
                    CHECK(died_in_fatal(data_open_no_folder));
                    unlink(cache);
                    rmdir(data);
                    rt_process.data_dir = NULL;
                }
            }
            unlink(file);
            rmdir(app0);
            rt_process.app0_dir = NULL;
        }
    }

    /* sceGnmGetTheTessellationFactorRingBufferBaseAddress: a reserved address the engine can map. */
    typedef uint64_t (*f_tf_base)(void);
    const f_tf_base tf_base = (f_tf_base)fn("libSceGnmDriver", "sceGnmGetTheTessellationFactorRingBufferBaseAddress");
    if (tf_base) {
        const uint64_t base = tf_base();
        CHECK(base && base < (1ull << 40) && base % 0x10000 == 0 && tf_base() == base);
    }
    /* sceGnmValidateCommandBuffers: "validation is not available" (0x80d13fff, the one value the
       executable's probe at 0x02abb8e0 tests for), whatever it is given; the buffers stay untouched. */
    typedef int32_t (*f_validate)(uint32_t, void **, uint32_t *, void **, uint32_t *);
    const f_validate validate = (f_validate)fn("libSceGnmDriver", "sceGnmValidateCommandBuffers");
    CHECK(validate != NULL);
    if (validate) {
        uint32_t words[4] = {0xc0001000, 0x11111111, 0x22222222, 0x33333333};
        void *buffer = words;
        uint32_t size = sizeof words;
        CHECK((uint32_t)validate(1, &buffer, &size, NULL, NULL) == 0x80d13fffu);
        CHECK((uint32_t)validate(0, NULL, NULL, NULL, NULL) == 0x80d13fffu);
        CHECK(words[0] == 0xc0001000 && words[3] == 0x33333333 && size == sizeof words && buffer == words);
    }
    /* sceGnmDispatchInitDefaultHardwareState, sceGnmDrawInitDefaultHardwareState200: write nothing
       (no GPU to set up) and say so. */
    typedef uint32_t (*f_init_state)(uint32_t *, uint64_t);
    static const char *const init_state_names[] = {"sceGnmDispatchInitDefaultHardwareState",
                                                   "sceGnmDrawInitDefaultHardwareState200"};
    for (int n = 0; n < 2; n++) {
        const f_init_state init_state = (f_init_state)fn("libSceGnmDriver", init_state_names[n]);
        CHECK(init_state != NULL);
        if (!init_state) continue;
        uint32_t packets[256];
        for (int i = 0; i < 256; i++) packets[i] = 0xa5a5a5a5u;
        CHECK(init_state(packets, 256) == 0);
        CHECK(packets[0] == 0xa5a5a5a5u && packets[255] == 0xa5a5a5a5u);
    }
    /* sceGnmSetCsShader: the reserved 25 dwords become one PM4 NOP packet; answers 0. */
    typedef int32_t (*f_set_cs)(uint32_t *, uint32_t, const void *);
    const f_set_cs set_cs = (f_set_cs)fn("libSceGnmDriver", "sceGnmSetCsShader");
    CHECK(set_cs != NULL);
    if (set_cs) {
        uint32_t packet[26];
        for (int i = 0; i < 26; i++) packet[i] = 0xa5a5a5a5u;
        CHECK(set_cs(packet, 25, (const void *)packet) == 0);
        CHECK(packet[0] == (0xc0001000u | (23u << 16)) && packet[1] == 0 && packet[24] == 0);
        CHECK(packet[25] == 0xa5a5a5a5u);
    }
    /* sceKernelReadTsc: the CPU's time-stamp counter, which moves forward. */
    typedef uint64_t (*f_tsc)(void);
    const f_tsc read_tsc = (f_tsc)fn("libkernel", "sceKernelReadTsc");
    CHECK(read_tsc != NULL);
    if (read_tsc) {
        const uint64_t first = read_tsc();
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
        CHECK(read_tsc() > first);
    }
    /* The other command-buffer writers: each fills exactly the reserved dwords with one NOP packet
       (header with the count minus two, zero payload), answers 0 and writes nothing past them. */
    static const char *const writers[] = {
        "sceGnmSetEsShader", "sceGnmSetGsShader", "sceGnmSetHsShader", "sceGnmSetLsShader", "sceGnmSetPsShader",
        "sceGnmSetVsShader", "sceGnmUpdateGsShader", "sceGnmUpdateHsShader", "sceGnmUpdatePsShader",
        "sceGnmUpdateVsShader", "sceGnmInsertPushMarker", "sceGnmInsertPopMarker", "sceGnmInsertSetMarker",
        "sceGnmInsertPushColorMarker", "sceGnmInsertSetColorMarker", "sceGnmInsertWaitFlipDone"};
    typedef int32_t (*f_writer)(uint32_t *, uint32_t, const void *, uint64_t);
    for (size_t n = 0; n < sizeof writers / sizeof writers[0]; n++) {
        const f_writer writer = (f_writer)fn("libSceGnmDriver", writers[n]);
        CHECK(writer != NULL);
        if (!writer) continue;
        uint32_t packet[8];
        for (int i = 0; i < 8; i++) packet[i] = 0xa5a5a5a5u;
        CHECK(writer(packet, 7, "label", 0) == 0);
        CHECK(packet[0] == (0xc0001000u | (5u << 16)) && packet[1] == 0 && packet[6] == 0 && packet[7] == 0xa5a5a5a5u);
    }
    /* sceGnmIsUserPaEnabled: off. */
    typedef int32_t (*f_user_pa)(void);
    const f_user_pa user_pa = (f_user_pa)fn("libSceGnmDriver", "sceGnmIsUserPaEnabled");
    CHECK(user_pa != NULL && user_pa() == 0);
    /* sceGnmMapComputeQueue: ids from 1 (the engine treats 0 as failure); a pipe and queue once. */
    cq_fn = (f_cq_t)fn("libSceGnmDriver", "sceGnmMapComputeQueue");
    if (cq_fn) {
        static uint32_t ring_a[0x400], ring_b[0x400];
        static volatile uint32_t read_a, read_b;
        const int32_t qa = cq_fn(0, 0, ring_a, 0x400, &read_a), qb = cq_fn(0, 1, ring_b, 0x400, &read_b);
        CHECK(qa > 0 && qb > 0 && qa != qb);
        CHECK(died_in_fatal(cq_twice));
        CHECK(died_in_fatal(cq_no_ring));
        /* sceGnmDingDong: each indirect buffer goes to the renderer like a submitted one (with no
           display, its work is done at once): the RELEASE_MEM "done" value reaches its label, and the
           read pointer reaches the write offset, also when the ring wraps. */
        ding_dong_fn = (f_ding_dong)fn("libSceGnmDriver", "sceGnmDingDong");
        CHECK(ding_dong_fn != NULL);
        if (ding_dong_fn) {
            static volatile uint32_t label;
            static volatile uint64_t wide_label;
            const uint64_t label_at = (uint64_t)(uintptr_t)&label, wide_at = (uint64_t)(uintptr_t)&wide_label;
            enum { JOB_DWORDS = 19, JOB_LABEL_VALUE = 15 };
            static uint32_t job[JOB_DWORDS];
            const uint32_t job_packets[JOB_DWORDS] = {
                0xc0001000u, 0,                                    /* NOP */
                0xc0017600u, 0x242, 0x1234,                        /* SET_SH_REG */
                0xc0031500u, 1, 1, 1, 1,                           /* DISPATCH_DIRECT */
                0xc0054900u, 0x0003862fu, 0x20000000u,             /* RELEASE_MEM, 32-bit data to memory */
                (uint32_t)label_at, (uint32_t)(label_at >> 32), 7, 0,
                0xc0001000u, 0};                                   /* NOP */
            memcpy(job, job_packets, sizeof job);
            static uint32_t wide_job[7];
            const uint32_t wide_packets[7] = {0xc0054900u, 0x0003862fu, 0x40000000u, /* 64-bit data */
                (uint32_t)wide_at, (uint32_t)(wide_at >> 32), 0x89abcdefu, 0x01234567u};
            memcpy(wide_job, wide_packets, sizeof wide_job);
            dd_queue = qa; dd_ring = ring_a;
            put_indirect_buffer(ring_a, job, JOB_DWORDS);
            put_indirect_buffer(ring_a + 4, job, 0); /* an empty command buffer */
            ding_dong_fn((uint32_t)qa, 8);
            CHECK(label == 7 && read_a == 8);
            /* Fill the rest of the ring, then wrap to its start, as the engine's submit does: every
               slot from 8 to the end completes, and so does the first slot after the wrap. */
            for (uint32_t slot = 8; slot < 0x3fc; slot += 4) put_indirect_buffer(ring_a + slot, job, 0);
            put_indirect_buffer(ring_a + 0x3fc, wide_job, 7);
            job[JOB_LABEL_VALUE] = 9;
            put_indirect_buffer(ring_a, job, JOB_DWORDS);
            ding_dong_fn((uint32_t)qa, 4);
            CHECK(wide_label == 0x0123456789abcdefull && label == 9 && read_a == 4);
            CHECK(died_in_fatal(dd_unknown_packet));
            CHECK(died_in_fatal(dd_bad_queue));
        }
    }

    /* sceKernelCreateEqueue / WaitEqueue / DeleteEqueue. */
    const f_eq_create eq_create = (f_eq_create)fn("libkernel", "sceKernelCreateEqueue");
    const f_eq_delete eq_delete = (f_eq_delete)fn("libkernel", "sceKernelDeleteEqueue");
    eq_wait_fn = (f_eq_wait)fn("libkernel", "sceKernelWaitEqueue");
    if (eq_create && eq_delete && eq_wait_fn) {
        CHECK(eq_create(&eq_test_queue, "flip") == 0 && eq_test_queue);
        int marker;
        CHECK(rt_equeue_add(eq_test_queue, RT_EVFILT_DISPLAY, 6, &marker) == 0);
        CHECK(rt_equeue_trigger(eq_test_queue, RT_EVFILT_GRAPHICS_CORE, 1, 0) == -1); /* not registered */
        pthread_t trigger;
        if (pthread_create(&trigger, NULL, eq_trigger_later, NULL)) { perror("pthread_create"); return 1; }
        unsigned char ev[2][32];
        int32_t n = -1;
        CHECK(eq_wait_fn(eq_test_queue, ev, 1, &n, NULL) == 0 && n == 1); /* woken by the other thread */
        pthread_join(trigger, NULL);
        uint64_t ident; int16_t filter; int64_t data; void *udata;
        memcpy(&ident, ev[0], 8); memcpy(&filter, ev[0] + 8, 2); memcpy(&data, ev[0] + 16, 8); memcpy(&udata, ev[0] + 24, 8);
        CHECK(ident == 6 && filter == -13 && data == 42 && udata == &marker);
        /* Two triggered events come back oldest first, each once. */
        CHECK(rt_equeue_add(eq_test_queue, RT_EVFILT_GRAPHICS_CORE, 1, NULL) == 0);
        CHECK(rt_equeue_trigger(eq_test_queue, RT_EVFILT_GRAPHICS_CORE, 1, 7) == 0);
        CHECK(rt_equeue_trigger(eq_test_queue, RT_EVFILT_DISPLAY, 6, 8) == 0);
        CHECK(eq_wait_fn(eq_test_queue, ev, 2, &n, NULL) == 0 && n == 2);
        memcpy(&filter, ev[0] + 8, 2); memcpy(&data, ev[1] + 16, 8);
        CHECK(filter == -14 && data == 8);
        CHECK(died_in_fatal(eq_wait_timeout));
        CHECK(died_in_fatal(eq_wait_no_count));
        CHECK(eq_delete(eq_test_queue) == 0);
    }

    /* The attribute leaf accepts raw DWORDs, supports unaligned caller storage, and
       clears only its sixteen reserved bytes. The public void API ignores RAX;
       the recovered native leaf leaves zero-extended pitch there. */
    attribute = (attribute_wire)fn("libSceVideoOut", "sceVideoOutSetBufferAttribute");
    CHECK(attribute);
    if (attribute) {
        CHECK(died_in_fatal(video_attribute_null));
        const uint32_t fields[] = {UINT32_MAX, 0x80000001u, 0x12345678u, 0, 1, 0xfedcba98u};
        unsigned char bytes[48];
        memset(bytes, 0xa5, sizeof bytes);
        CHECK(attribute(bytes + 3, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]) == fields[5]);
        CHECK(!memcmp(bytes + 3, fields, sizeof fields));
        for (size_t i = 0; i < 16; ++i) CHECK(bytes[3 + sizeof fields + i] == 0);
        for (size_t i = 0; i < 3; ++i) CHECK(bytes[i] == 0xa5);
        for (size_t i = 43; i < sizeof bytes; ++i) CHECK(bytes[i] == 0xa5);
    }

    /* libSceVideoOut: the executable's frame cycle. */
    vo_open_fn = (f_vo_open)fn("libSceVideoOut", "sceVideoOutOpen");
    const f_vo_rate vo_rate = (f_vo_rate)fn("libSceVideoOut", "sceVideoOutSetFlipRate");
    const f_vo_event vo_event = (f_vo_event)fn("libSceVideoOut", "sceVideoOutAddFlipEvent");
    const f_vo_attr vo_attr = (f_vo_attr)fn("libSceVideoOut", "sceVideoOutSetBufferAttribute");
    const f_vo_register vo_register = (f_vo_register)fn("libSceVideoOut", "sceVideoOutRegisterBuffers");
    vo_flip_fn = (f_vo_flip)fn("libSceVideoOut", "sceVideoOutSubmitFlip");
    vo_status_fn = (f_vo_status)fn("libSceVideoOut", "sceVideoOutGetFlipStatus");
    const f_eq_create vo_eq_create = (f_eq_create)fn("libkernel", "sceKernelCreateEqueue");
    if (vo_open_fn && vo_rate && vo_event && vo_attr && vo_register && vo_flip_fn && vo_status_fn && vo_eq_create && eq_wait_fn) {
        /* The display runs headless and captures each presented frame. */
        static char capture_buf[512];
        const char *tmp_dir = getenv("TMPDIR");
        snprintf(capture_buf, sizeof capture_buf, "%s/pb-display-XXXXXX", tmp_dir ? tmp_dir : "/tmp");
        char *capture = mkdtemp(capture_buf);
        CHECK(capture != NULL && rt_display_configure(RT_DISPLAY_HEADLESS, capture) == 0);
        CHECK(rt_display_configure(RT_DISPLAY_WINDOW, NULL) == -1); /* the tests build without a window */
        CHECK(rt_display_configure(RT_DISPLAY_HEADLESS, capture) == 0);
        vo_handle = vo_open_fn(0, 0, 0, NULL);
        CHECK(vo_handle > 0 && vo_rate(vo_handle, 0) == 0);
        rt_equeue *flips = NULL;
        CHECK(vo_eq_create(&flips, "flip") == 0 && vo_event(flips, vo_handle, NULL) == 0);
        uint32_t attr[10];
        memset(attr, 0xee, sizeof attr);
        vo_attr(attr, 0x80000000u, 0, 0, 1920, 1080, 1920);
        CHECK(attr[0] == 0x80000000u && attr[1] == 0 && attr[3] == 1920 && attr[4] == 1080 && attr[5] == 1920 && attr[6] == 0);
        static uint32_t frame_a[16], frame_b[16];
        void *const buffers[2] = {frame_a, frame_b};
        CHECK(vo_register(vo_handle, 0, buffers, 2, attr) == 0);
        /* Each registered buffer has a black host frame of the attribute's size. */
        uint32_t *const host_a = rt_display_frame(vo_handle, 0), *const host_b = rt_display_frame(vo_handle, 1);
        CHECK(host_a && host_b && host_a != host_b && host_b[0] == 0 && host_b[1920 * 1080 - 1] == 0);
        CHECK(rt_display_frame(vo_handle, 2) == NULL && rt_display_presented(vo_handle) == 0);
        if (host_b) host_b[1920 + 1] = 0x00336699u; /* pixel (1, 1): red 0x33, green 0x66, blue 0x99 */
        {   /* before the first flip: success, and all zero (no flips yet) */
            unsigned char before[64], zero[64] = {0};
            memset(before, 0xee, sizeof before);
            CHECK(vo_status_fn(vo_handle, before) == 0 && !memcmp(before, zero, sizeof zero));
        }
        CHECK(vo_flip_fn(vo_handle, 1, 2, 77) == 0);
        unsigned char ev[32]; int32_t n = 0; int64_t data;
        CHECK(eq_wait_fn(flips, ev, 1, &n, NULL) == 0 && n == 1 && (memcpy(&data, ev + 16, 8), data == 77));
        unsigned char st[64]; uint64_t count; int64_t arg; int32_t current;
        CHECK(vo_status_fn(vo_handle, st) == 0);
        memcpy(&count, st, 8); memcpy(&arg, st + 0x18, 8); memcpy(&current, st + 0x38, 4);
        CHECK(count == 1 && arg == 77 && current == 1);
        /* The flip presented buffer 1: counted, and captured as frame 0 with the pixel in place
           (written by the display's own thread: waited for). */
        CHECK(rt_display_presented(vo_handle) == 1);
        rt_display_capture_wait();
        if (capture) {
            char frame_path[600];
            snprintf(frame_path, sizeof frame_path, "%s/frame-000000.ppm", capture);
            FILE *ppm = fopen(frame_path, "rb");
            CHECK(ppm != NULL);
            if (ppm) {
                char magic[3] = {0};
                unsigned w = 0, h = 0, max = 0;
                CHECK(fscanf(ppm, "%2s %u %u %u", magic, &w, &h, &max) == 4 && !strcmp(magic, "P6"));
                CHECK(w == 1920 && h == 1080 && max == 255);
                fgetc(ppm); /* the single whitespace before the pixels */
                unsigned char rgb[3];
                CHECK(fseek(ppm, (long)(1920 + 1) * 3, SEEK_CUR) == 0 && fread(rgb, 1, 3, ppm) == 3);
                CHECK(rgb[0] == 0x33 && rgb[1] == 0x66 && rgb[2] == 0x99);
                fclose(ppm);
            }
            remove(frame_path);
        }
        /* Back-to-back flips queue their frames for the display's writer thread (more of them than it
           has slots): once waited for, each is on disk, in order, with the pixel it had at its flip. */
        if (capture && host_a) {
            enum { QUICK_FLIPS = 7 };
            for (uint32_t k = 0; k < QUICK_FLIPS; k++) {
                host_a[1920 + 1] = 0x00010203u * (k + 1);
                CHECK(vo_flip_fn(vo_handle, 0, 2, 100 + k) == 0); /* hsync: no vblank wait */
            }
            int32_t got = 0; /* the flip events: one pending event per queue, as a kqueue keeps them */
            CHECK(eq_wait_fn(flips, ev, 1, &got, NULL) == 0 && got == 1);
            rt_display_capture_wait();
            for (uint32_t k = 0; k < QUICK_FLIPS; k++) {
                char frame_path[600];
                snprintf(frame_path, sizeof frame_path, "%s/frame-%06u.ppm", capture, k + 1);
                FILE *ppm = fopen(frame_path, "rb");
                CHECK(ppm != NULL);
                if (!ppm) continue;
                char magic[3] = {0};
                unsigned w = 0, h = 0, max = 0;
                unsigned char rgb[3] = {0};
                CHECK(fscanf(ppm, "%2s %u %u %u", magic, &w, &h, &max) == 4 && w == 1920 && h == 1080);
                fgetc(ppm);
                CHECK(fseek(ppm, (long)(1920 + 1) * 3, SEEK_CUR) == 0 && fread(rgb, 1, 3, ppm) == 3);
                CHECK(rgb[0] == k + 1 && rgb[1] == 2 * (k + 1) && rgb[2] == 3 * (k + 1));
                fclose(ppm);
                remove(frame_path);
            }
        }
        rt_display_configure(RT_DISPLAY_HEADLESS, NULL); /* stop capturing: writing a frame would skew the timing */
        /* A vsync flip completes at the next 60 Hz tick: within one period. */
        struct timespec before, after;
        clock_gettime(CLOCK_MONOTONIC, &before);
        CHECK(vo_flip_fn(vo_handle, 0, 1, 78) == 0);
        clock_gettime(CLOCK_MONOTONIC, &after);
        const int64_t waited_ns = (after.tv_sec - before.tv_sec) * 1000000000 + (after.tv_nsec - before.tv_nsec);
        CHECK(waited_ns <= 1000000000 / 60 + 2000000);
        CHECK(eq_wait_fn(flips, ev, 1, &n, NULL) == 0 && n == 1 && (memcpy(&data, ev + 16, 8), data == 78));
        /* A second vsync flip right after waits for the following tick: about one period later. */
        CHECK(vo_flip_fn(vo_handle, 1, 1, 80) == 0);
        clock_gettime(CLOCK_MONOTONIC, &before);
        CHECK(eq_wait_fn(flips, ev, 1, &n, NULL) == 0); /* pending flip events of one port merge: take each */
        CHECK(vo_flip_fn(vo_handle, 0, 1, 81) == 0);
        clock_gettime(CLOCK_MONOTONIC, &after);
        const int64_t apart_ns = (after.tv_sec - before.tv_sec) * 1000000000 + (after.tv_nsec - before.tv_nsec);
        CHECK(apart_ns >= 1000000000 / 60 - 2000000);
        CHECK(eq_wait_fn(flips, ev, 1, &n, NULL) == 0);
        CHECK(died_in_fatal(vo_flip_other_mode));

        /* sceGnmSubmitCommandBuffers and sceGnmSubmitAndFlipCommandBuffers complete the work at once:
           draw state and draws are skipped, a met wait passes, the end-of-work value is written; the
           frame's flip follows. An unknown packet and an unmet wait stop by name. */
        gnm_submit_fn = (f_gnm_submit)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
        typedef int32_t (*f_gnm_flip)(uint32_t, void **, uint32_t *, void **, uint32_t *, int32_t, int32_t, int32_t, int64_t);
        const f_gnm_flip gnm_flip = (f_gnm_flip)fn("libSceGnmDriver", "sceGnmSubmitAndFlipCommandBuffers");
        typedef int32_t (*f_gnm_done)(void);
        const f_gnm_done gnm_done = (f_gnm_done)fn("libSceGnmDriver", "sceGnmSubmitDone");
        CHECK(gnm_submit_fn && gnm_flip && gnm_done);
        if (gnm_submit_fn && gnm_flip && gnm_done) {
            static volatile uint64_t end_of_frame;
            static uint32_t wait_target = 7;
            const uint64_t eop = (uint64_t)(uintptr_t)&end_of_frame, wt = (uint64_t)(uintptr_t)&wait_target;
            uint32_t frame[] = {
                0xc0053c00u, 0x15, (uint32_t)wt, (uint32_t)(wt >> 32), 7, 0xffffffffu, 10, /* wait: >= 7, holds */
                0xc0016900u, 0x1, 0x2,                                                    /* SET_CONTEXT_REG */
                0xc0017900u, 0x3, 0x4,                                                    /* SET_UCONFIG_REG */
                0xc0002f00u, 0x1,                                                         /* NUM_INSTANCES */
                0xc0012d00u, 0x3, 0x2,                                                    /* DRAW_INDEX_AUTO */
                0xc0004600u, 0x0,                                                         /* EVENT_WRITE */
                0xc0044700u, 0x528, (uint32_t)eop, (uint32_t)(eop >> 32) | 0x40000000u, 0x9, 0x1, /* EOP 64-bit */
            };
            void *buffers[1] = {frame};
            uint32_t bytes[1] = {sizeof frame};
            end_of_frame = 0;
            CHECK(gnm_submit_fn(1, buffers, bytes, NULL, NULL) == 0 && end_of_frame == 0x100000009ull);
            end_of_frame = 0;
            CHECK(gnm_flip(1, buffers, bytes, NULL, NULL, vo_handle, 1, 2, 79) == 0 && end_of_frame == 0x100000009ull);
            CHECK(gnm_done() == 0);
            CHECK(eq_wait_fn(flips, ev, 1, &n, NULL) == 0 && n == 1 && (memcpy(&data, ev + 16, 8), data == 79));
            CHECK(vo_status_fn(vo_handle, st) == 0 && (memcpy(&arg, st + 0x18, 8), arg == 79));
            CHECK(died_in_fatal(gnm_submit_unknown));
            /* 70 buffers in one call, as the character creation screen submits them: each one's
               end-of-work write lands, in order (the last value stays) */
            {
                enum { MANY = 70 };
                static uint32_t many[MANY][6];
                void *many_buffers[MANY];
                uint32_t many_bytes[MANY];
                for (uint32_t k = 0; k < MANY; k++) {
                    const uint32_t eop_words[6] = {0xc0044700u, 0x528, (uint32_t)eop, (uint32_t)(eop >> 32) | 0x40000000u, k + 1, 0x1};
                    memcpy(many[k], eop_words, sizeof eop_words);
                    many_buffers[k] = many[k], many_bytes[k] = sizeof many[k];
                }
                end_of_frame = 0;
                CHECK(gnm_submit_fn(MANY, many_buffers, many_bytes, NULL, NULL) == 0 && end_of_frame == 0x100000000ull + MANY);
            }
            /* the constant arrays present with empty entries, as 0x0149bf3a passes them: no constant buffer */
            void *no_constants[1] = {NULL};
            uint32_t no_constant_bytes[1] = {0};
            CHECK(gnm_submit_fn(1, buffers, bytes, no_constants, no_constant_bytes) == 0);
            CHECK(died_in_fatal(gnm_submit_with_constant));
            /* DMA_DATA from memory into CP_DMA_READ_TAGS, as the title sends it: only a cache read */
            static uint32_t dma_source[25] = {1, 2, 3};
            const uint64_t dma_from = (uint64_t)(uintptr_t)dma_source;
            uint32_t dma[7] = {0xc0055000u, 0x60000000u, (uint32_t)dma_from, (uint32_t)(dma_from >> 32), 0x3022cu, 0, 0x28200064u};
            CHECK(gnm_submit_one_result(dma, 7) == 0 && dma_source[0] == 1 && dma_source[2] == 3);
            CHECK(died_in_fatal(gnm_submit_dma_fill));
            CHECK(died_in_fatal(gnm_submit_dma_other_register));
            /* an indexed draw: index type, base and buffer size, then DRAW_INDEX_2; only memory reads */
            static uint16_t indices[3] = {0, 1, 2};
            const uint64_t index_at = (uint64_t)(uintptr_t)indices;
            uint32_t indexed[] = {
                0xc0002a00u, 0x0,                                                   /* INDEX_TYPE 16-bit */
                0xc0012600u, (uint32_t)index_at, (uint32_t)(index_at >> 32),        /* INDEX_BASE */
                0xc0001300u, 3,                                                     /* INDEX_BUFFER_SIZE */
                0xc0032700u, 3, (uint32_t)index_at, 3, 0,                           /* DRAW_INDEX_2 */
                0xc0033500u, 3, 0, 3, 0,                                            /* DRAW_INDEX_OFFSET_2 */
            };
            CHECK(gnm_submit_one_result(indexed, sizeof indexed / 4) == 0 && indices[2] == 2);
            CHECK(died_in_fatal(gnm_submit_constant_without_sizes));
            CHECK(died_in_fatal(gnm_submit_unmet_wait));
        }
        CHECK(died_in_fatal(vo_flip_bad_buffer));
        CHECK(died_in_fatal(vo_open_other));
        rt_display_capture_wait();
        if (capture) { /* every later flip was captured too */
            for (uint64_t k = 0; k < rt_display_presented(vo_handle); k++) {
                char frame_path[600];
                snprintf(frame_path, sizeof frame_path, "%s/frame-%06llu.ppm", capture, (unsigned long long)k);
                remove(frame_path);
            }
            CHECK(rmdir(capture) == 0);
        }
        rt_display_configure(RT_DISPLAY_NONE, NULL);
    }

    rtc_now = (f_rtc_now)fn("libSceRtc", "sceRtcGetCurrentClockLocalTime");
    rtc_tick = (f_rtc_tick)fn("libSceRtc", "sceRtcGetTick");
    const f_rtc_resolution rtc_resolution = (f_rtc_resolution)fn("libSceRtc", "sceRtcGetTickResolution");
    rtc_set = (f_rtc_set)fn("libSceRtc", "sceRtcSetTick");
    rtc_weekday = (f_rtc_weekday)fn("libSceRtc", "sceRtcGetDayOfWeek");
    CHECK(rtc_now && rtc_tick && rtc_resolution && rtc_set && rtc_weekday);
    if (rtc_now && rtc_tick && rtc_resolution && rtc_set && rtc_weekday) {
        /* 0 Sunday to 6 Saturday, against dates whose weekday is known */
        CHECK(rtc_weekday(1, 1, 1) == 1);       /* Monday, the proleptic Gregorian calendar's first day */
        CHECK(rtc_weekday(1970, 1, 1) == 4);    /* Thursday */
        CHECK(rtc_weekday(2000, 2, 29) == 2);   /* Tuesday */
        CHECK(rtc_weekday(2015, 3, 24) == 2);   /* Tuesday */
        CHECK(rtc_weekday(2026, 11, 6) == 5);   /* Friday */
        CHECK(rtc_weekday(2026, 11, 8) == 0);   /* Sunday */
        CHECK(rtc_weekday(9999, 12, 31) == 5);  /* Friday */
        for (int d = 1; d <= 31; d++) {         /* against the host's calendar, a whole month */
            struct tm day = {.tm_year = 2024 - 1900, .tm_mon = 0, .tm_mday = d, .tm_hour = 12};
            timegm(&day);
            CHECK(rtc_weekday(2024, 1, d) == day.tm_wday);
        }
        CHECK(died_in_fatal(rtc_weekday_february_29_2023));
        CHECK(died_in_fatal(rtc_weekday_month_13));
        CHECK(rtc_resolution() == 1000000);
        uint64_t tick = 7;
        const rtc_time year_one = {1, 1, 1, 0, 0, 0, 0};
        CHECK(rtc_tick(&year_one, &tick) == 0 && tick == 0);
        const rtc_time unix_epoch = {1970, 1, 1, 0, 0, 0, 0};
        CHECK(rtc_tick(&unix_epoch, &tick) == 0 && tick == 62135596800000000ull); /* 719162 days */
        const rtc_time leap_day = {2024, 2, 29, 23, 59, 59, 999999};
        CHECK(rtc_tick(&leap_day, &tick) == 0 && tick == (62135596800ull + 1709251199ull) * 1000000ull + 999999ull);
        const rtc_time y2000_leap = {2000, 2, 29, 12, 0, 0, 0};
        CHECK(rtc_tick(&y2000_leap, &tick) == 0 && tick == (62135596800ull + 951825600ull) * 1000000ull);
        /* the local time, compared with the host's own */
        rtc_time now;
        memset(&now, 0xee, sizeof now);
        const time_t before = time(NULL);
        CHECK(rtc_now(&now) == 0);
        struct tm local;
        localtime_r(&before, &local);
        CHECK(now.year == local.tm_year + 1900 && now.month >= 1 && now.month <= 12 && now.day >= 1 && now.day <= 31);
        CHECK(now.hour <= 23 && now.minute <= 59 && now.second <= 59 && now.microsecond < 1000000);
        CHECK(rtc_tick(&now, &tick) == 0);
        /* sceRtcSetTick reverses sceRtcGetTick, for dates across leap years and centuries */
        const rtc_time samples[] = {
            {1, 1, 1, 0, 0, 0, 0}, {1600, 12, 31, 23, 59, 59, 999999}, {1601, 1, 1, 0, 0, 0, 0},
            {1900, 2, 28, 12, 0, 0, 5}, {1900, 3, 1, 0, 0, 0, 0}, {2000, 2, 29, 1, 2, 3, 4},
            {2024, 12, 31, 23, 59, 59, 1}, {9999, 12, 31, 23, 59, 59, 999999}, now,
        };
        for (size_t s = 0; s < sizeof samples / sizeof *samples; s++) {
            uint64_t k = 0;
            rtc_time back;
            memset(&back, 0xee, sizeof back);
            CHECK(rtc_tick(&samples[s], &k) == 0 && rtc_set(&back, &k) == 0 && memcmp(&back, &samples[s], sizeof back) == 0);
        }
        const uint64_t epoch_tick = 62135596800000000ull;
        rtc_time epoch;
        CHECK(rtc_set(&epoch, &epoch_tick) == 0 && epoch.year == 1970 && epoch.month == 1 && epoch.day == 1 && epoch.hour == 0);
        CHECK(died_in_fatal(rtc_set_year_10000));
        CHECK(died_in_fatal(rtc_set_no_tick));
        CHECK(died_in_fatal(rtc_now_nothing));
        CHECK(died_in_fatal(rtc_tick_february_30));
        CHECK(died_in_fatal(rtc_tick_leap_2100));
        CHECK(died_in_fatal(rtc_tick_microsecond));
        CHECK(died_in_fatal(rtc_tick_no_variable));
    }

    {   /* sceNpSetNpTitleId: the title id is kept; a missing argument is invalid */
        typedef int32_t (*f_np_title)(const void *, const void *);
        const f_np_title set_title = (f_np_title)fn("libSceNpManager", "sceNpSetNpTitleId");
        CHECK(set_title != NULL);
        if (set_title) {
            char kept[RT_NP_TITLE_ID_BYTES];
            rt_np_title_id(kept);
            CHECK(kept[0] == 0);
            unsigned char id[16], secret[128];
            memset(id, 0x7a, sizeof id);
            memcpy(id, "ABCD12345_00", 12);
            id[12] = 0;
            memset(secret, 0x5c, sizeof secret);
            CHECK(set_title(id, secret) == 0);
            rt_np_title_id(kept);
            CHECK(strcmp(kept, "ABCD12345_00") == 0);
            CHECK(set_title(NULL, secret) == (int32_t)0x80550003u && set_title(id, NULL) == (int32_t)0x80550003u);
        }
    }

    {   /* libSceNpMatching2: initialize and terminate, with the block's own size at 0x20 */
        typedef int32_t (*f_m2_init)(const void *);
        typedef int32_t (*f_m2_none)(void);
        const f_m2_init m2_init = (f_m2_init)fn("libSceNpMatching2", "sceNpMatching2Initialize");
        const f_m2_none m2_term = (f_m2_none)fn("libSceNpMatching2", "sceNpMatching2Terminate");
        CHECK(m2_init && m2_term);
        if (m2_init && m2_term) {
            uint64_t block[6] = {0x20000, 0, 0, 0x4000, 0x30, 0};
            CHECK(m2_term() == (int32_t)0x80550C03u);           /* not initialized */
            CHECK(m2_init(NULL) == (int32_t)0x80550C0Au);
            block[4] = 0x20;
            CHECK(m2_init(block) == (int32_t)0x80550C0Au);      /* a size other than 0x28 or 0x30 */
            block[4] = 0x30;
            CHECK(m2_init(block) == 0 && m2_init(block) == (int32_t)0x80550C02u);
            CHECK(m2_term() == 0 && m2_term() == (int32_t)0x80550C03u);
            block[4] = 0x28;
            CHECK(m2_init(block) == 0 && m2_term() == 0);        /* the older block size */
        }
    }
    {   /* libSceNpManager: callbacks kept but never called, the user signed out, nothing to deliver */
        typedef int32_t (*f_np_register)(void *, void *);
        typedef int32_t (*f_np_none)(void);
        typedef void (*f_np_presence)(void *, void *);
        const f_np_register reg = (f_np_register)fn("libSceNpManager", "sceNpRegisterStateCallback");
        const f_np_none unreg = (f_np_none)fn("libSceNpManager", "sceNpUnregisterStateCallback");
        const f_np_presence presence = (f_np_presence)fn("libSceNpManager", "sceNpRegisterGamePresenceCallback");
        const f_np_none check = (f_np_none)fn("libSceNpManager", "sceNpCheckCallback");
        np_state = (f_np_state)fn("libSceNpManager", "sceNpGetState");
        CHECK(reg && unreg && presence && check && np_state);
        if (reg && unreg && presence && check && np_state) {
            static int user_data;
            void *const callback = (void *)np_state_other_user; /* any address: it is never called */
            CHECK(reg(NULL, &user_data) == (int32_t)0x80550003u);
            CHECK(reg(callback, &user_data) == 0 && reg(callback, &user_data) == (int32_t)0x80550008u);
            presence(callback, &user_data);
            void *kept_data = NULL;
            CHECK(rt_np_callback(RT_NP_PRESENCE_CALLBACK, &kept_data) == callback && kept_data == &user_data);
            CHECK(rt_np_callback(RT_NP_STATE_CALLBACK, &kept_data) == callback && kept_data == &user_data);
            CHECK(check() == 0);
            CHECK(unreg() == 0 && unreg() == (int32_t)0x80550009u);
            CHECK(reg(callback, NULL) == 0 && unreg() == 0);     /* registered again after an unregister */
            uint32_t st = 77;
            CHECK(np_state(1, &st) == 0 && st == 1);             /* signed out */
            CHECK(np_state(1, NULL) == (int32_t)0x80550003u);
            CHECK(died_in_fatal(np_state_other_user));
        }
    }

    {
        const f_ep_create ep_create = (f_ep_create)fn("libSceNet", "sceNetEpollCreate");
        const f_ep_id_flags ep_abort = (f_ep_id_flags)fn("libSceNet", "sceNetEpollAbort");
        const f_ep_id ep_destroy = (f_ep_id)fn("libSceNet", "sceNetEpollDestroy");
        ep_wait = (f_ep_wait)fn("libSceNet", "sceNetEpollWait");
        net_errno_fn = (f_net_errno)fn("libSceNet", "sceNetErrnoLoc");
        CHECK(ep_create && ep_abort && ep_destroy && ep_wait && net_errno_fn);
        if (ep_create && ep_abort && ep_destroy && ep_wait && net_errno_fn) {
            int32_t *const err = net_errno_fn();
            CHECK(ep_create("x", 1) == (int32_t)0x80410116u && *err == 22);             /* EINVAL */
            CHECK(ep_create("a-name-of-thirty-two-characters!", 0) == (int32_t)0x8041013fu && *err == 63);
            const int32_t id = ep_create("a-name-of-thirty-one-character", 0);
            CHECK(id >= 1 && ep_create(NULL, 0) != id);
            unsigned char events[24 * 4];
            CHECK(ep_wait(id + 40, events, 4, 0) == (int32_t)0x80410109u && *err == 9); /* EBADF */
            CHECK(ep_wait(id, NULL, 4, 0) == (int32_t)0x80410116u);
            CHECK(ep_wait(id, events, 0, 0) == (int32_t)0x80410116u);
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            CHECK(ep_wait(id, events, 4, 20000) == 0);                                  /* times out, no events */
            clock_gettime(CLOCK_MONOTONIC, &t1);
            CHECK((t1.tv_sec - t0.tv_sec) * 1000000000 + (t1.tv_nsec - t0.tv_nsec) >= 20000000);
            CHECK(ep_abort(id, 0) == (int32_t)0x8041010fu && *err == 15);              /* ENOTBLK: none waiting */
            CHECK(ep_abort(id, 2) == (int32_t)0x80410116u);
            CHECK(ep_abort(id, 1) == 0 && ep_wait(id, events, 4, -1) == (int32_t)0x80410104u && *err == 4);
            CHECK(ep_wait(id, events, 4, 0) == 0);                                      /* the preserved abort is used up */
            /* a waiter for ever, interrupted by an abort from this thread */
            pthread_t waiter;
            ep_waiter_id = id;
            CHECK(pthread_create(&waiter, NULL, ep_waiter, (void *)(intptr_t)-1) == 0);
            int aborted = -1;
            for (int tries = 0; tries < 2000 && aborted != 0; tries++) {
                aborted = ep_abort(id, 0); /* ENOTBLK until the waiter is waiting */
                if (aborted != 0) usleep(1000);
            }
            CHECK(aborted == 0 && pthread_join(waiter, NULL) == 0);
            CHECK(ep_waiter_result == (int32_t)0x80410104u && ep_waiter_errno == 4);
            /* and one ended by a destroy: EINTR if it was waiting, EBADF if the destroy came first;
               either way it returns long before its 5-second timeout */
            CHECK(pthread_create(&waiter, NULL, ep_waiter, (void *)(intptr_t)5000000) == 0);
            usleep(20000);
            struct timespec d0, d1;
            clock_gettime(CLOCK_MONOTONIC, &d0);
            CHECK(ep_destroy(id) == 0 && pthread_join(waiter, NULL) == 0);
            clock_gettime(CLOCK_MONOTONIC, &d1);
            CHECK(ep_waiter_result == (int32_t)0x80410104u || ep_waiter_result == (int32_t)0x80410109u);
            CHECK(d1.tv_sec - d0.tv_sec < 1);
            CHECK(ep_destroy(id) == (int32_t)0x80410109u && ep_wait(id, events, 4, 0) == (int32_t)0x80410109u);
        }
        typedef int32_t (*f_sd_none)(void);
        typedef int32_t (*f_sd_init)(const void *);
        const f_sd_init sd_init = (f_sd_init)fn("libSceSaveData", "sceSaveDataInitialize");
        const f_sd_none sd_term = (f_sd_none)fn("libSceSaveData", "sceSaveDataTerminate");
        const f_sd_point sd_umount = (f_sd_point)fn("libSceSaveData", "sceSaveDataUmount");
        const f_sd_set_param sd_set_param = (f_sd_set_param)fn("libSceSaveData", "sceSaveDataSetParam");
        const f_sd_search sd_search = (f_sd_search)fn("libSceSaveData", "sceSaveDataDirNameSearch");
        const f_sd_setup sd_setup = (f_sd_setup)fn("libSceSaveData", "sceSaveDataSetupSaveDataMemory");
        const f_sd_memory sd_set = (f_sd_memory)fn("libSceSaveData", "sceSaveDataSetSaveDataMemory");
        sd_mount = (f_sd_mount)fn("libSceSaveData", "sceSaveDataMount");
        sd_get = (f_sd_memory)fn("libSceSaveData", "sceSaveDataGetSaveDataMemory");
        CHECK(sd_init && sd_term && sd_umount && sd_set_param && sd_search && sd_setup && sd_set && sd_mount && sd_get);
        static char saves[512];
        const char *tmp = getenv("TMPDIR");
        snprintf(saves, sizeof saves, "%s/pb-saves-XXXXXX", tmp ? tmp : "/tmp");
        CHECK(mkdtemp(saves) != NULL);
        rt_process.savedata_dir = saves;
        if (sd_init && sd_term && sd_umount && sd_set_param && sd_search && sd_setup && sd_set && sd_mount && sd_get) {
            /* before initialize: the title's SDK decides the code (the test binary has none: before 2.00) */
            char point[16];
            CHECK(sd_mount_dir("SAVE0", 4, point) == (int32_t)0x809F000Bu);
            /* from here on, a title built against SDK 2.00, as the process parameters say */
            static uint64_t fake_image[8];
            const uint32_t prefix[4] = {32, 0, 0x4942524fu, 1};            /* size 32, magic "ORBI", version 1 */
            const uint32_t sdk_2_00 = 0x02000071u;
            memcpy(fake_image, prefix, sizeof prefix);
            memcpy((char *)fake_image + 16, &sdk_2_00, sizeof sdk_2_00);
            void *saved_image = rt_process.image, *saved_param = rt_process.proc_param;
            const uint64_t saved_size = rt_process.image_size;
            rt_process.image = fake_image, rt_process.image_size = sizeof fake_image, rt_process.proc_param = fake_image;
            CHECK(sd_mount_dir("SAVE0", 4, point) == (int32_t)0x80960002u); /* SDK 2.00 to 2.49 */
            CHECK(sd_init(NULL) == 0);
            CHECK(sd_mount_dir("SAVE0", 2, point) == (int32_t)0x809F0008u);       /* not there, no create */
            CHECK(sd_mount_dir("", 4, point) == (int32_t)0x809F0000u);
            CHECK(sd_mount_dir("../up", 4, point) == (int32_t)0x809F0000u);
            CHECK(sd_mount_dir("a/b", 4, point) == (int32_t)0x809F0000u);
            sd_mount_req negative = {.user = -1, .dir = "SAVE0", .mode = 4};
            sd_mount_res unused;
            CHECK(sd_mount(&negative, &unused) == (int32_t)0x809F0011u);
            CHECK(sd_mount_dir("SAVE0", 4 | 2, point) == 0 && !strcmp(point, "/savedata0"));
            CHECK(sd_mount_dir("SAVE0", 2, NULL) == (int32_t)0x809F0003u);        /* already mounted */
            {   /* a title thread's stack can be small: a mount must fit in 64 KiB */
                pthread_attr_t small;
                pthread_t thread;
                CHECK(pthread_attr_init(&small) == 0 && pthread_attr_setstacksize(&small, 64 * 1024) == 0);
                CHECK(pthread_create(&thread, &small, sd_small_stack_mount, NULL) == 0 && pthread_join(thread, NULL) == 0);
                CHECK(sd_small_stack_result == 0);
                pthread_attr_destroy(&small);
            }
            /* the title's own file through the mount, landing in the save folder */
            typedef int32_t (*f_sd_close)(int32_t);
            const f_kopen open_fn = (f_kopen)fn("libkernel", "sceKernelOpen");
            const f_sd_close close_fn = (f_sd_close)fn("libkernel", "sceKernelClose");
            if (open_fn && close_fn) {
                const int32_t fd = open_fn("/savedata0/userdata0010", 0x0001 | 0x0200, 0666); /* write, create */
                CHECK(fd >= 0 && close_fn(fd) == 0);
                char host[700];
                snprintf(host, sizeof host, "%s/1/SAVE0/userdata0010", saves);
                struct stat created;
                CHECK(stat(host, &created) == 0 && S_ISREG(created.st_mode));
            }
            /* params: a title and a user param, kept in sce_sys/param.bin */
            CHECK(sd_set_param("/savedata0", 1, "The Old Hunters", 16) == 0);
            const uint32_t user_param = 7;
            CHECK(sd_set_param("/savedata0", 4, &user_param, 4) == 0);
            CHECK(sd_set_param("/savedata0", 5, &user_param, 4) == (int32_t)0x809F0000u);
            CHECK(sd_set_param("/savedata9", 1, "x", 2) == (int32_t)0x809F0004u);
            CHECK(sd_umount("/savedata0") == 0 && sd_umount("/savedata0") == (int32_t)0x809F0008u);
            CHECK(sd_mount_dir("SAVE0", 4, NULL) == (int32_t)0x809F0007u);        /* create, but it exists */
            /* a second save, read-only mount, and the 16-slot limit */
            CHECK(sd_mount_dir("SAVE1", 4, point) == 0 && sd_umount(point) == 0);
            CHECK(sd_mount_dir("SAVE1", 1, point) == 0 && !strcmp(point, "/savedata0"));
            if (open_fn) CHECK(open_fn("/savedata0/new", 0x0001 | 0x0200, 0666) == (int32_t)0x8002001eu); /* EROFS */
            CHECK(sd_umount(point) == 0);
            char name[16];
            for (int k = 0; k < 16; k++) {
                snprintf(name, sizeof name, "SLOT%d", k);
                CHECK(sd_mount_dir(name, 4, NULL) == 0);
            }
            CHECK(sd_mount_dir("SLOT16", 4, NULL) == (int32_t)0x809F000Cu);
            CHECK(sd_term() == 0);                                                /* unmounts them (before SDK 4.00) */
            CHECK(sd_init(NULL) == 0 && sd_mount_dir("SLOT0", 2, point) == 0 && sd_umount(point) == 0);
            /* search: by name pattern, sorted, with params */
            char names[4][32];
            sd_param params[4];
            sd_cond cond = {.user = 1, .dir = "SAVE%", .key = 0, .order = 1};
            sd_result res = {.names = names, .capacity = 4, .params = params};
            CHECK(sd_search(&cond, &res) == 0 && res.hits == 2 && res.set == 2);
            CHECK(!strcmp(names[0], "SAVE1") && !strcmp(names[1], "SAVE0"));    /* descending */
            CHECK(!strcmp(params[1].title, "The Old Hunters") && params[1].user_param == 7);
            cond.dir = "save_";                                                   /* case-insensitive, one character */
            res.capacity = 1;
            CHECK(sd_search(&cond, &res) == 0 && res.hits == 2 && res.set == 1);
            cond.dir = NULL;
            res.capacity = 4;
            CHECK(sd_search(&cond, &res) == 0 && res.hits == 19);          /* with SMALLSTACK */
            cond.key = 6;
            CHECK(sd_search(&cond, &res) == (int32_t)0x809F0000u);
            /* the memory-backed save */
            char data[16] = "", back[16] = "";
            CHECK(sd_get(1, data, 4, 0) == (int32_t)0x809F0012u);                /* not set up */
            CHECK(sd_setup(1, 0, NULL) == (int32_t)0x809F0000u);
            CHECK(sd_setup(1, 4096, NULL) == 0);
            memcpy(data, "blood echoes", 13);
            CHECK(sd_set(1, data, 13, 100) == 0 && sd_get(1, back, 13, 100) == 0 && !strcmp(back, "blood echoes"));
            CHECK(sd_get(1, back, 4, 0) == 0 && !back[0] && !back[3]);           /* untouched bytes are zero */
            CHECK(sd_get(1, NULL, 4, 0) == (int32_t)0x809F0000u && sd_get(1, back, 4, -1) == (int32_t)0x809F0000u);
            CHECK(died_in_fatal(sd_get_past_end));
            CHECK(died_in_fatal(sd_mount_other_title));
            CHECK(died_in_fatal(sd_mount_other_user));
            CHECK(sd_term() == 0);
            rt_process.image = saved_image, rt_process.image_size = saved_size, rt_process.proc_param = saved_param;
        }
        rt_process.savedata_dir = NULL;
    }

    te_exit = (f_thread_exit)fn("libkernel", "scePthreadExit");
    CHECK(te_exit != NULL);
    if (te_exit && tc_create && kc_create && kc_set) {
        static int value;
        CHECK(kc_create(&te_key, te_destructor) == 0);
        void *thread = NULL, *attr = tc_attr(1, 1, 1); /* detached */
        CHECK(tc_create(&thread, &attr, te_entry, &value, "exits") == 0);
        for (int ms = 0; ms < 5000 && !atomic_load(&te_destructor_calls); ms++) usleep(1000); /* up to 5 s on a slow machine */
        CHECK(atomic_load(&te_destructor_calls) == 1 && te_destroyed == &value);
        CHECK(!atomic_load(&te_after_exit));
        CHECK(died_in_fatal(te_exit_main));
    }

    pg_init = (f_pg_init)fn("libScePlayGo", "scePlayGoInitialize");
    pg_open = (f_pg_open)fn("libScePlayGo", "scePlayGoOpen");
    pg_chunks = (f_pg_chunks)fn("libScePlayGo", "scePlayGoGetChunkId");
    pg_locus = (f_pg_locus)fn("libScePlayGo", "scePlayGoGetLocus");
    CHECK(pg_init && pg_open && pg_chunks && pg_locus);
    if (pg_init && pg_open && pg_chunks && pg_locus) {
        CHECK(died_in_fatal(pg_open_first)); /* before scePlayGoInitialize */
        static unsigned char work[0x200000];
        const struct { void *buffer; uint32_t size, reserved; } params = {work, sizeof work, 0};
        CHECK(pg_init(&params) == 0);
        CHECK(died_in_fatal(pg_init_nothing));
        CHECK(pg_open(&pg_handle, NULL) == 0 && pg_handle == 1);
        CHECK(died_in_fatal(pg_open_with_params));
        static char app0[512];
        const char *tmp = getenv("TMPDIR");
        snprintf(app0, sizeof app0, "%s/pb-playgo-XXXXXX", tmp ? tmp : "/tmp");
        CHECK(mkdtemp(app0) != NULL);
        rt_process.app0_dir = app0;
        CHECK(died_in_fatal(pg_chunks_no_manifest)); /* no sce_sys/playgo-manifest.xml yet */
        char path[600];
        snprintf(path, sizeof path, "%s/sce_sys", app0);
        CHECK(mkdir(path, 0755) == 0);
        snprintf(path, sizeof path, "%s/sce_sys/playgo-manifest.xml", app0);
        FILE *manifest = fopen(path, "w");
        if (manifest) {
            fputs("<psproject fmt=\"playgo-manifest\"><volume><chunk_info chunk_count=\"3\" scenario_count=\"1\"/></volume></psproject>",
                  manifest);
            fclose(manifest);
        }
        uint16_t ids[100];
        uint32_t count = 0;
        memset(ids, 0xee, sizeof ids);
        CHECK(pg_chunks(pg_handle, ids, 100, &count) == 0 && count == 3 && ids[0] == 0 && ids[1] == 1 && ids[2] == 2 &&
              ids[3] == 0xeeee);
        int8_t loci[3] = {0, 0, 0};
        CHECK(pg_locus(pg_handle, ids, 3, loci) == 0 && loci[0] == 3 && loci[1] == 3 && loci[2] == 3);
        CHECK(died_in_fatal(pg_chunks_short));
        CHECK(died_in_fatal(pg_chunks_bad_handle));
        CHECK(died_in_fatal(pg_locus_unknown));
        typedef int32_t (*f_pg_speed)(int32_t, int32_t);
        const f_pg_speed pg_speed = (f_pg_speed)fn("libScePlayGo", "scePlayGoSetInstallSpeed");
        CHECK(pg_speed && pg_speed(pg_handle, 1) == 0 && pg_speed(pg_handle, 2) == 0 && pg_speed(pg_handle, 0) == 0);
        CHECK(pg_speed && pg_speed(pg_handle, 3) == (int32_t)0x80B20004u && pg_speed(pg_handle + 1, 2) == (int32_t)0x80B20009u);
        remove(path);
        snprintf(path, sizeof path, "%s/sce_sys", app0);
        rmdir(path);
        rmdir(app0);
        rt_process.app0_dir = NULL;
    }

    for (syslib_test *test = registered_tests; test; test = test->next) test->run();

    if (!failures) puts("syslib: ok");
    return failures != 0;
}
