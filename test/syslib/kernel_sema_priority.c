/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Kernel semaphores that wake by thread priority (attr 2, runtime/syslib/kernel_sema.c): guest
   threads at priorities 700, 300, 500 and 300 wait on an empty semaphore and are served best
   priority first (the smallest number), the two at 300 in the order they arrived. */
#include "../syslib_test.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>

typedef int32_t (*f_create_sema)(void **, const char *, uint32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_wait_sema)(void *, int32_t, uint32_t *);
typedef int32_t (*f_count_sema)(void *, int32_t);
typedef int (*f_attr)(void **);
typedef int (*f_attr_stack)(void **, uint64_t);
typedef int (*f_attr_int)(void **, int);
typedef int (*f_thread_create)(void **, void **, void *(*)(void *), void *, const char *);
typedef void *(*f_thread_self)(void);
typedef int (*f_set_priority)(void *, int32_t);

/* The runtime's semaphore object, mirrored so the test can see how many threads are waiting (keep
   in step with kernel_sema.c and test_syslib.c's sema_mirror). */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int32_t count, max;
    uint32_t attr;
    uint64_t next_ticket;
    void *queue;
    int waiters;
} sema_layout;

enum { WAITERS = 4 };
static void *prio_sema;
static f_wait_sema wait_fn;
static f_thread_self self_fn;
static f_set_priority set_priority_fn;
static int served[WAITERS];
static atomic_int served_count;

static int waiting(void)
{
    sema_layout *s = prio_sema;
    pthread_mutex_lock(&s->lock);
    const int n = s->waiters;
    pthread_mutex_unlock(&s->lock);
    return n;
}

typedef struct { int tag; int32_t priority; } waiter_plan;

static void *waiter(void *argument)
{
    const waiter_plan *plan = argument;
    set_priority_fn(self_fn(), plan->priority);
    if (wait_fn(prio_sema, 1, NULL) == 0) served[atomic_fetch_add(&served_count, 1)] = plan->tag;
    return NULL;
}

SYSLIB_TEST(kernel_sema_priority)
{
    const f_create_sema create = (f_create_sema)fn("libkernel", "sceKernelCreateSema");
    const f_count_sema signal = (f_count_sema)fn("libkernel", "sceKernelSignalSema");
    const f_attr attr_init = (f_attr)fn("libkernel", "scePthreadAttrInit");
    const f_attr_stack attr_stack = (f_attr_stack)fn("libkernel", "scePthreadAttrSetstacksize");
    const f_attr_int attr_detach = (f_attr_int)fn("libkernel", "scePthreadAttrSetdetachstate");
    const f_thread_create thread_create = (f_thread_create)fn("libkernel", "scePthreadCreate");
    wait_fn = (f_wait_sema)fn("libkernel", "sceKernelWaitSema");
    self_fn = (f_thread_self)fn("libkernel", "scePthreadSelf");
    set_priority_fn = (f_set_priority)fn("libkernel", "scePthreadSetprio");
    if (failures) return;
    CHECK(create(&prio_sema, "prio", 2, 0, WAITERS, NULL) == 0);
    /* tags 1..4 arrive in this order; 2 and 4 share the best priority, so 2 goes before 4 */
    static waiter_plan plans[WAITERS] = {{1, 700}, {2, 300}, {3, 500}, {4, 300}};
    void *threads[WAITERS] = {0};
    for (int i = 0; i < WAITERS; i++) {
        void *attr = NULL;
        attr_init(&attr);
        attr_stack(&attr, 0x10000);
        attr_detach(&attr, 1); /* detached: the runtime has no join (the title never joins) */
        CHECK(thread_create(&threads[i], &attr, waiter, &plans[i], "waiter") == 0);
        while (waiting() < i + 1) sched_yield(); /* in arrival order */
    }
    for (int i = 0; i < WAITERS; i++) {
        CHECK(signal(prio_sema, 1) == 0);
        while (atomic_load(&served_count) < i + 1) sched_yield();
    }
    CHECK(served[0] == 2 && served[1] == 4 && served[2] == 3 && served[3] == 1);
}
