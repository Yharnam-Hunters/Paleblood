/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(__APPLE__) || !defined(__MACH__) || !defined(__x86_64__)
#error Guest-pointer TSD qualification requires a Darwin x86-64 process.
#endif
#include "runtime/guest_pointer.h"
#include "runtime/guest_tls.h"
#include "runtime/process.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* Retained until process exit, as are the runtime's thread handles. No key deletion can race
   workers or code rewritten with its displacement. TCB storage remains with each caller. */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static rt_guest_tls_key *key;
static uint32_t offset;
static int initialized;
static _Thread_local int qualified;

static void initialize(void)
{
    rt_guest_tls_candidate candidate;
    if (rt_guest_tls_create(&key).status != RT_GUEST_TLS_OK) return;
    if (rt_guest_tls_get_candidate(key, &candidate).status != RT_GUEST_TLS_OK) return;
    offset = candidate.candidate_offset;
    initialized = 1;
}

static void require_key(void)
{
    if (pthread_once(&once, initialize) || !initialized)
        rt_fatal("guest thread pointer", "cannot prepare an owned representable public pthread key");
}

static uintptr_t direct_value(void)
{
    uintptr_t value;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(value) : "r"((uintptr_t)offset) : "memory");
    return value;
}

static int agrees(void *expected)
{
    const rt_guest_tls_result changed = expected ? rt_guest_tls_bind(key, expected) : rt_guest_tls_clear(key);
    if (changed.status != RT_GUEST_TLS_OK) return 0;
    void *public_value = NULL;
    return rt_guest_tls_read(key, &public_value).status == RT_GUEST_TLS_OK
        && public_value == expected && direct_value() == (uintptr_t)expected;
}

void rt_guest_pointer_bind(void *tcb)
{
    if (!tcb) rt_fatal("guest thread pointer", "no TCB to bind");
    require_key();
    qualified = 0;
    uint64_t witnesses[2][2] = {{UINT64_C(0x1122334455667788), 1},
                                {UINT64_C(0x8877665544332211), 2}};
    if (!agrees(witnesses[0]) || !agrees(witnesses[1]) || !agrees(NULL) || !agrees(tcb)) {
        /* Do not leave a stack witness behind after a recoverable named stop. */
        if (rt_guest_tls_clear(key).status != RT_GUEST_TLS_OK)
            rt_fatal("guest thread pointer", "cannot clear a failed public/direct qualification");
        rt_fatal("guest thread pointer", "public/direct TSD qualification failed on this host thread");
    }
    qualified = 1;
}

uint32_t rt_guest_pointer_offset(void)
{
    if (!qualified) rt_fatal("guest thread pointer", "calling host thread has no qualified guest TCB");
    return offset;
}

void rt_guest_pointer_clear(void)
{
    require_key();
    if (!agrees(NULL)) rt_fatal("guest thread pointer", "cannot clear the public/direct guest TCB");
    qualified = 0;
}
