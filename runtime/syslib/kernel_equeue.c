/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Kernel event queues (libkernel).

   Facts used: the PS4 kernel is FreeBSD's, whose kqueue delivers struct kevent: ident (64-bit),
   filter (16-bit), flags (16-bit), fflags (32-bit), data (64-bit), udata (a pointer), 32 bytes on
   amd64. The OpenOrbis homebrew SDK's libkernel.h declares sceKernelCreateEqueue(queue *, name)
   and sceKernelDeleteEqueue(queue); its _types/kernel.h lists the filters (display -13, graphics
   core -14, ...). sceKernelWaitEqueue(queue, events, count, &returned, timeout) is the PS4's
   kevent wait: the executable passes room for one event, a variable for how many came back and no
   timeout (0x015e41b6, 0x02ad6f26), right after asking video out for a flip, so the call blocks
   until the flip is done.

   The runtime's queue is a list of registrations, added and triggered by the runtime's own event
   sources (runtime/include/runtime/equeue.h). A wait returns the triggered events, oldest first,
   and clears them (each is reported once per trigger). An event's flags and fflags are 0: what the
   PS4 sets there is not established, and no caller reads them. A timeout (no call passes one),
   deleting a queue a thread is waiting on, an event array or count that does not fit, and the
   error values stop by name. */
#include "runtime/equeue.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_REGISTRATIONS = 32, MAX_NAME = 32 };

typedef struct {
    uint64_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t data;
    void *udata;
} guest_kevent;
_Static_assert(sizeof(guest_kevent) == 32, "FreeBSD amd64 struct kevent is 32 bytes");

typedef struct {
    int16_t filter;
    uint64_t ident;
    void *udata;
    int triggered;
    uint64_t order;  /* trigger order, so a wait returns the oldest first */
    int64_t data;
} registration;

struct rt_equeue {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    char name[MAX_NAME];
    registration regs[MAX_REGISTRATIONS];
    int count;
    int waiters;
    uint64_t next_order;
};

static int32_t create_equeue(rt_equeue **out, const char *name)
{
    static const char fn[] = "sceKernelCreateEqueue";
    if (!out) rt_fatal(fn, "no variable for the queue: the error value is not established");
    if (!name) rt_fatal(fn, "no name: whether one is required is not established");
    rt_equeue *q = calloc(1, sizeof *q);
    if (!q) rt_fatal(fn, "the host has no memory for the queue");
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->changed, NULL);
    strncpy(q->name, name, MAX_NAME - 1);
    *out = q;
    return 0;
}

static int32_t delete_equeue(rt_equeue *q)
{
    static const char fn[] = "sceKernelDeleteEqueue";
    if (!q) rt_fatal(fn, "no queue: the error value is not established");
    pthread_mutex_lock(&q->lock);
    const int busy = q->waiters;
    pthread_mutex_unlock(&q->lock);
    if (busy) rt_fatal(fn, "a queue a thread is waiting on: what the waiter is told is not established");
    pthread_cond_destroy(&q->changed);
    pthread_mutex_destroy(&q->lock);
    free(q);
    return 0;
}

int rt_equeue_add(rt_equeue *q, int16_t filter, uint64_t ident, void *udata)
{
    pthread_mutex_lock(&q->lock);
    int r = 0, i = 0;
    while (i < q->count && !(q->regs[i].filter == filter && q->regs[i].ident == ident)) i++;
    if (i < q->count) q->regs[i].udata = udata;
    else if (q->count == MAX_REGISTRATIONS) r = -1;
    else q->regs[q->count++] = (registration){filter, ident, udata, 0, 0, 0};
    pthread_mutex_unlock(&q->lock);
    return r;
}

int rt_equeue_trigger(rt_equeue *q, int16_t filter, uint64_t ident, int64_t data)
{
    pthread_mutex_lock(&q->lock);
    int r = -1;
    for (int i = 0; i < q->count; i++)
        if (q->regs[i].filter == filter && q->regs[i].ident == ident) {
            if (!q->regs[i].triggered) q->regs[i].order = q->next_order++;
            q->regs[i].triggered = 1;
            q->regs[i].data = data;
            r = 0;
        }
    if (!r) pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->lock);
    return r;
}

static int32_t wait_equeue(rt_equeue *q, guest_kevent *events, int32_t room, int32_t *returned, const void *timeout)
{
    static const char fn[] = "sceKernelWaitEqueue";
    if (!q) rt_fatal(fn, "no queue: the error value is not established");
    if (!events || room < 1 || !returned) rt_fatal(fn, "no room for an event or for the count: the error value is not established");
    if (timeout) rt_fatal(fn, "a timeout: not used by the executable, its unit and answer are not established");
    pthread_mutex_lock(&q->lock);
    q->waiters++;
    for (;;) {
        int got = 0;
        while (got < room) {
            int oldest = -1;
            for (int i = 0; i < q->count; i++)
                if (q->regs[i].triggered && (oldest < 0 || q->regs[i].order < q->regs[oldest].order)) oldest = i;
            if (oldest < 0) break;
            registration *const e = &q->regs[oldest];
            events[got++] = (guest_kevent){e->ident, e->filter, 0, 0, e->data, e->udata};
            e->triggered = 0;
        }
        if (got) {
            *returned = got;
            break;
        }
        pthread_cond_wait(&q->changed, &q->lock);
    }
    q->waiters--;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

RT_SYSLIB("libkernel", sceKernelCreateEqueue, create_equeue);
RT_SYSLIB("libkernel", sceKernelDeleteEqueue, delete_equeue);
RT_SYSLIB("libkernel", sceKernelWaitEqueue, wait_equeue);
