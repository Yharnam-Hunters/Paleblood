/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Kernel semaphores (libkernel).

   Facts used:
   - The signatures: int32_t sceKernelCreateSema(OrbisKernelSema *sema, const char *name, uint32_t
     attr, int32_t initial, int32_t max, const OrbisKernelSemaOptParam *option); int32_t
     sceKernelDeleteSema(OrbisKernelSema); int32_t sceKernelWaitSema(OrbisKernelSema, int32_t need,
     OrbisKernelUseconds *timeout); int32_t sceKernelSignalSema(OrbisKernelSema, int32_t count);
     int32_t sceKernelPollSema(OrbisKernelSema, int32_t need). The OpenOrbis homebrew SDK header
     (include/orbis/libkernel.h) declares them so, and names attr 1 and 2 as FIFO and priority wake
     order (ORBIS_KERNEL_SEMA_ATTR_TH_FIFO, _TH_PRIO); it corroborates the shapes, not the behaviour.
   - The executable's own calls create with attr 1 or 2, an initial count and a maximum and no
     option, wait for one unit with no timeout, signal one unit or a count, delete, and compare
     every result with 0.

   A semaphore is a counter behind a host mutex and condition variable. Waiters are served in the
   order they arrived; every case whose PS4 answer is not established stops by the function's name
   instead of guessing: a timeout, a missing semaphore or option, deleting one with waiters,
   signalling past the maximum, a poll that cannot be satisfied, a second waiter when the order is
   by thread priority (guest priorities are not modelled), a wait for more than one unit behind
   another waiter (whether it may be passed is not established), and the error values. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

enum { SEMA_ATTR_TH_FIFO = 0x01, SEMA_ATTR_TH_PRIO = 0x02 };

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int32_t count, max;
    uint32_t attr;
    uint64_t next_ticket, serving; /* arrival order of waiters */
    int waiters;
} guest_sema;

static guest_sema *require(guest_sema *sema, const char *caller)
{
    if (!sema) rt_fatal(caller, "no semaphore: the error value is not established");
    return sema;
}

static int32_t create_sema(guest_sema **out, const char *name, uint32_t attr, int32_t initial, int32_t max,
                           const void *option)
{
    static const char fn[] = "sceKernelCreateSema";
    if (!out) rt_fatal(fn, "no variable for the semaphore: the error value is not established");
    if (!name) rt_fatal(fn, "no name: whether one is required is not established");
    if (attr != SEMA_ATTR_TH_FIFO && attr != SEMA_ATTR_TH_PRIO) rt_fatal(fn, "an attribute that is not established");
    if (option) rt_fatal(fn, "an option parameter: its fields are not established");
    if (max <= 0 || initial < 0 || initial > max)
        rt_fatal(fn, "counts outside 0 <= initial <= max, max > 0: the error value is not established");
    guest_sema *s = calloc(1, sizeof *s);
    if (!s) rt_fatal(fn, "the host has no memory for the semaphore");
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->changed, NULL);
    s->count = initial;
    s->max = max;
    s->attr = attr;
    *out = s;
    return 0;
}

static int32_t delete_sema(guest_sema *sema)
{
    static const char fn[] = "sceKernelDeleteSema";
    guest_sema *s = require(sema, fn);
    pthread_mutex_lock(&s->lock);
    if (s->waiters) rt_fatal(fn, "a semaphore with waiters: what they are told is not established");
    pthread_mutex_unlock(&s->lock);
    pthread_cond_destroy(&s->changed);
    pthread_mutex_destroy(&s->lock);
    free(s);
    return 0;
}

static int32_t wait_sema(guest_sema *sema, int32_t need, const uint32_t *timeout)
{
    static const char fn[] = "sceKernelWaitSema";
    guest_sema *s = require(sema, fn);
    if (timeout) rt_fatal(fn, "a timeout: how the remaining time is reported is not established");
    if (need <= 0 || need > s->max) rt_fatal(fn, "a count outside 1..max: the error value is not established");
    pthread_mutex_lock(&s->lock);
    if (s->count < need) {
        if (s->waiters && s->attr == SEMA_ATTR_TH_PRIO)
            rt_fatal(fn, "a second waiter woken by thread priority: guest priorities are not modelled");
        if (s->waiters && need > 1)
            rt_fatal(fn, "a wait for more than one unit behind another waiter: whether it may be passed is not established");
    }
    const uint64_t ticket = s->next_ticket++;
    s->waiters++;
    while (ticket != s->serving || s->count < need) pthread_cond_wait(&s->changed, &s->lock);
    s->count -= need;
    s->serving++;
    s->waiters--;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->lock);
    return 0;
}

static int32_t signal_sema(guest_sema *sema, int32_t count)
{
    static const char fn[] = "sceKernelSignalSema";
    guest_sema *s = require(sema, fn);
    if (count <= 0) rt_fatal(fn, "a count below 1: the error value is not established");
    pthread_mutex_lock(&s->lock);
    if (count > s->max - s->count) rt_fatal(fn, "a signal past the maximum: the error value is not established");
    s->count += count;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->lock);
    return 0;
}

static int32_t poll_sema(guest_sema *sema, int32_t need)
{
    static const char fn[] = "sceKernelPollSema";
    guest_sema *s = require(sema, fn);
    if (need <= 0 || need > s->max) rt_fatal(fn, "a count outside 1..max: the error value is not established");
    pthread_mutex_lock(&s->lock);
    if (s->waiters || s->count < need) rt_fatal(fn, "not enough units now: the error value is not established");
    s->count -= need;
    pthread_mutex_unlock(&s->lock);
    return 0;
}

RT_SYSLIB("libkernel", sceKernelCreateSema, create_sema);
RT_SYSLIB("libkernel", sceKernelDeleteSema, delete_sema);
RT_SYSLIB("libkernel", sceKernelWaitSema, wait_sema);
RT_SYSLIB("libkernel", sceKernelSignalSema, signal_sema);
RT_SYSLIB("libkernel", sceKernelPollSema, poll_sema);
