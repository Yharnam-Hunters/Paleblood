/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_EQUEUE_H
#define RUNTIME_EQUEUE_H

/* Kernel event queues (runtime/syslib/kernel_equeue.c), for the runtime's own event sources: a
   system library registers an event on the guest's queue, then triggers it when the thing it
   stands for happens, waking any thread in sceKernelWaitEqueue. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The public filter numbers (OpenOrbis' _types/kernel.h). */
enum { RT_EVFILT_DISPLAY = -13, RT_EVFILT_GRAPHICS_CORE = -14 };

typedef struct rt_equeue rt_equeue;

/* Registers (filter, ident) on the queue with the guest's user data; registering the same pair again
   replaces its user data. Returns 0, or -1 when the queue is full. */
int rt_equeue_add(rt_equeue *queue, int16_t filter, uint64_t ident, void *udata);

/* Marks (filter, ident) triggered with data and wakes the queue's waiters. Returns 0, or -1 when
   the pair is not registered. */
int rt_equeue_trigger(rt_equeue *queue, int16_t filter, uint64_t ident, int64_t data);

#ifdef __cplusplus
}
#endif

#endif
