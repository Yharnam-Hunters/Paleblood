/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Thread lifecycle (runtime/syslib/kernel_thread.c): scePthreadJoin and scePthreadDetach, the POSIX
   pthread_create, pthread_join, pthread_exit and pthread_attr_setschedparam, explicit scheduling
   (scePthreadAttrSetinheritsched), and scePthreadOnce. */
#include "../syslib_test.h"

#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

typedef void *(*entry_fn)(void *);
typedef int (*f_attr)(void **);
typedef int (*f_attr_u64)(void **, uint64_t);
typedef int (*f_attr_int)(void **, int);
typedef int (*f_attr_param)(void **, const int32_t *);
typedef int (*f_create)(void **, void **, entry_fn, void *, const char *);
typedef int (*f_posix_create)(void **, void **, entry_fn, void *);
typedef int (*f_join)(void *, void **);
typedef int (*f_detach)(void *);
typedef void (*f_exit)(void *);
typedef void *(*f_self)(void);
typedef int (*f_getprio)(void *, int32_t *);
typedef int (*f_once)(int32_t *, void (*)(void));

#define SCE(e) ((int)(0x80020000u + (e)))
enum { ESRCH_ = 3, EDEADLK_ = 11, EINVAL_ = 22, ENOTSUP_ = 45 };

static f_attr a_init;
static f_attr_u64 a_stack;
static f_attr_int a_detach, a_inherit;
static f_attr_param a_param, a_posix_param;
static f_create create;
static f_posix_create posix_create;
static f_join join, posix_join;
static f_detach detach;
static f_exit posix_exit;
static f_self self;
static f_getprio getprio;
static f_once once;

static atomic_int gate; /* the workers below wait until it opens */

/* An attribute with a 64 KiB stack and the detach state. */
static void *attribute(int detached)
{
    void *a = NULL;
    if (a_init(&a) || a_stack(&a, 0x10000) || a_detach(&a, detached)) return NULL;
    return a;
}

static void wait_for_gate(void)
{
    for (int ms = 0; ms < 10000 && !atomic_load(&gate); ms++) usleep(1000);
}

static void *returns_argument(void *argument) { return argument; }
static void *waits_then_returns(void *argument) { wait_for_gate(); return argument; }
static void *exits_with_seven(void *argument) { (void)argument; posix_exit((void *)7); return NULL; }

static void *joined_value;
static atomic_int joiner_answer = -1;
static atomic_int joiner_started;
static void *joins_argument(void *argument)
{
    atomic_store(&joiner_started, 1);
    atomic_store(&joiner_answer, join(argument, &joined_value));
    return NULL;
}

static atomic_int priority_seen;
static void *records_priority(void *argument)
{
    (void)argument;
    int32_t p = 0;
    getprio(self(), &p);
    atomic_store(&priority_seen, p);
    return NULL;
}

/* Until the thread has ended and been collected: a join of a detached thread answers ESRCH then. */
static int collected_within_seconds(void *thread)
{
    for (int ms = 0; ms < 5000; ms++, usleep(1000))
        if (join(thread, NULL) == SCE(ESRCH_)) return 1;
    return 0;
}

static void detach_main(void) { detach(self()); }
static void *joins_main(void *main_thread) { join(main_thread, NULL); return NULL; }
static void join_main_from_worker(void)
{
    void *t = NULL, *a = attribute(0);
    create(&t, &a, joins_main, self(), "joins-main");
    join(t, NULL);
}
static void explicit_without_priority(void)
{
    void *t = NULL, *a = attribute(0);
    a_inherit(&a, 0);
    create(&t, &a, returns_argument, NULL, "explicit");
}
static void inherit_unknown_value(void)
{
    void *a = attribute(0);
    a_inherit(&a, 1);
}

static atomic_int init_calls;
static void slow_init(void)
{
    atomic_fetch_add(&init_calls, 1);
    wait_for_gate();
}
static int32_t shared_once;
static atomic_int second_caller_returned;
static void *calls_once(void *argument)
{
    (void)argument;
    once(&shared_once, slow_init);
    atomic_store(&second_caller_returned, 1);
    return NULL;
}
static void *first_caller(void *argument) { (void)argument; once(&shared_once, slow_init); return NULL; }

SYSLIB_TEST(thread_join)
{
    a_init = (f_attr)fn("libkernel", "scePthreadAttrInit");
    a_stack = (f_attr_u64)fn("libkernel", "scePthreadAttrSetstacksize");
    a_detach = (f_attr_int)fn("libkernel", "scePthreadAttrSetdetachstate");
    a_inherit = (f_attr_int)fn("libkernel", "scePthreadAttrSetinheritsched");
    a_param = (f_attr_param)fn("libkernel", "scePthreadAttrSetschedparam");
    a_posix_param = (f_attr_param)fn("libScePosix", "pthread_attr_setschedparam");
    create = (f_create)fn("libkernel", "scePthreadCreate");
    posix_create = (f_posix_create)fn("libScePosix", "pthread_create");
    join = (f_join)fn("libkernel", "scePthreadJoin");
    posix_join = (f_join)fn("libScePosix", "pthread_join");
    detach = (f_detach)fn("libkernel", "scePthreadDetach");
    posix_exit = (f_exit)fn("libScePosix", "pthread_exit");
    self = (f_self)fn("libkernel", "scePthreadSelf");
    getprio = (f_getprio)fn("libkernel", "scePthreadGetprio");
    once = (f_once)fn("libkernel", "scePthreadOnce");
    const char *names[][2] = {{"libkernel", "scePthreadJoin"}, {"libkernel", "scePthreadDetach"},
                              {"libkernel", "scePthreadAttrSetinheritsched"}, {"libkernel", "scePthreadOnce"},
                              {"libScePosix", "pthread_create"}, {"libScePosix", "pthread_join"},
                              {"libScePosix", "pthread_exit"}, {"libScePosix", "pthread_attr_setschedparam"}};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) CHECK(is_implemented(names[i][0], names[i][1]));
    if (!a_init || !a_stack || !a_detach || !a_inherit || !a_param || !a_posix_param || !create || !posix_create ||
        !join || !posix_join || !detach || !posix_exit || !self || !getprio || !once)
        return;

    /* A joinable thread: the join waits, gives the value, and collects it. */
    void *t = NULL, *a = attribute(0), *value = NULL;
    CHECK(create(&t, &a, returns_argument, (void *)0x1234, "returns") == 0);
    CHECK(join(t, &value) == 0 && value == (void *)0x1234);
    CHECK(join(t, &value) == SCE(ESRCH_) && detach(t) == SCE(ESRCH_));
    static uint64_t not_a_thread[64]; /* zeroed memory shaped like nothing the runtime made */
    CHECK(join(NULL, NULL) == SCE(EINVAL_) && join(self(), NULL) == SCE(EDEADLK_));
    CHECK(join(not_a_thread, NULL) == SCE(ESRCH_) && detach(not_a_thread) == SCE(ESRCH_));
    CHECK(detach(NULL) == SCE(EINVAL_));

    /* Created detached: no join, no second detach; collected when it ends. */
    atomic_store(&gate, 0);
    a = attribute(1);
    CHECK(create(&t, &a, waits_then_returns, NULL, "detached") == 0);
    CHECK(join(t, NULL) == SCE(EINVAL_) && detach(t) == SCE(EINVAL_));
    atomic_store(&gate, 1);
    CHECK(collected_within_seconds(t));

    /* Detached after creation, the same. */
    atomic_store(&gate, 0);
    a = attribute(0);
    CHECK(create(&t, &a, waits_then_returns, NULL, "detach-later") == 0);
    CHECK(detach(t) == 0 && join(t, NULL) == SCE(EINVAL_) && detach(t) == SCE(EINVAL_));
    atomic_store(&gate, 1);
    CHECK(collected_within_seconds(t));

    /* A second joiner is ENOTSUP, and a detach while a thread waits to join EINVAL. */
    atomic_store(&gate, 0);
    void *waiter = NULL, *joiner = NULL;
    a = attribute(0);
    CHECK(create(&waiter, &a, waits_then_returns, (void *)0x55, "waited-on") == 0);
    a = attribute(0);
    CHECK(create(&joiner, &a, joins_argument, waiter, "joiner") == 0);
    for (int ms = 0; ms < 5000 && !atomic_load(&joiner_started); ms++) usleep(1000);
    usleep(100000); /* the joiner is in its join by now */
    CHECK(join(waiter, NULL) == SCE(ENOTSUP_) && detach(waiter) == SCE(EINVAL_));
    atomic_store(&gate, 1);
    CHECK(join(joiner, NULL) == 0 && atomic_load(&joiner_answer) == 0 && joined_value == (void *)0x55);

    /* The POSIX names: creation without a name, pthread_exit's value, bare error numbers. */
    a = attribute(0);
    CHECK(posix_create(&t, &a, exits_with_seven, NULL) == 0);
    CHECK(posix_join(t, &value) == 0 && value == (void *)7);
    CHECK(posix_join(t, NULL) == ESRCH_ && posix_join(NULL, NULL) == EINVAL_);
    void *empty = NULL;
    const int32_t priority = 0x200;
    CHECK(a_posix_param(&empty, &priority) == EINVAL_ && a_param(&empty, &priority) == SCE(EINVAL_));

    /* Explicit scheduling starts the thread at the attribute's priority; inherited, at the creator's. */
    int32_t mine = 0;
    CHECK(getprio(self(), &mine) == 0);
    a = attribute(0);
    CHECK(a_param(&a, &priority) == 0 && a_inherit(&a, 0) == 0);
    CHECK(create(&t, &a, records_priority, NULL, "explicit") == 0 && join(t, NULL) == 0);
    CHECK(atomic_load(&priority_seen) == 0x200);
    a = attribute(0);
    CHECK(a_posix_param(&a, &priority) == 0 && a_inherit(&a, 4) == 0);
    CHECK(create(&t, &a, records_priority, NULL, "inherit") == 0 && join(t, NULL) == 0);
    CHECK(atomic_load(&priority_seen) == mine);
    CHECK(a_inherit(&empty, 0) == SCE(EINVAL_));
    CHECK(died_in_fatal(explicit_without_priority));
    CHECK(died_in_fatal(inherit_unknown_value));

    /* The main thread's end is not modelled. */
    CHECK(died_in_fatal(detach_main));
    CHECK(died_in_fatal(join_main_from_worker));

    /* scePthreadOnce: once only, a caller during the routine waits for its end, other states EINVAL. */
    atomic_store(&gate, 1);
    int32_t done_once = 0;
    CHECK(once(&done_once, slow_init) == 0 && done_once == 1 && atomic_load(&init_calls) == 1);
    CHECK(once(&done_once, slow_init) == 0 && atomic_load(&init_calls) == 1);
    int32_t bad = 7;
    CHECK(once(&bad, slow_init) == SCE(EINVAL_) && atomic_load(&init_calls) == 1);
    atomic_store(&gate, 0);
    void *first = NULL, *later = NULL;
    a = attribute(0);
    CHECK(create(&first, &a, first_caller, NULL, "once-first") == 0);
    for (int ms = 0; ms < 5000 && atomic_load(&init_calls) < 2; ms++) usleep(1000);
    a = attribute(0);
    CHECK(create(&later, &a, calls_once, NULL, "once-later") == 0);
    usleep(50000);
    CHECK(!atomic_load(&second_caller_returned) && shared_once != 0 && shared_once != 1);
    atomic_store(&gate, 1);
    CHECK(join(first, NULL) == 0 && join(later, NULL) == 0);
    CHECK(atomic_load(&second_caller_returned) && shared_once == 1 && atomic_load(&init_calls) == 2);
}
