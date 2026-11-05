/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(__APPLE__) || !defined(__MACH__) || !defined(__x86_64__)
#error This owned TSD implementation requires a Darwin x86-64 process.
#endif
#include "runtime/guest_tls.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

struct rt_guest_tls_key {
    pthread_key_t native;
    _Atomic size_t bindings;
};

enum { PROFILE_FIRST_KEY = 256, PROFILE_LAST_KEY = 767, SLOT_BYTES = 8 };
static rt_guest_tls_result result(rt_guest_tls_status status, int native_error, int saved_errno)
{
    errno = saved_errno;
    return (rt_guest_tls_result){status, native_error};
}

rt_guest_tls_result rt_guest_tls_create(rt_guest_tls_key **out)
{
    const int saved = errno;
    if (!out || *out) return result(RT_GUEST_TLS_INVALID_ARGUMENT, 0, saved);
    rt_guest_tls_key *key = calloc(1, sizeof *key);
    if (!key) return result(RT_GUEST_TLS_NO_MEMORY, 0, saved);
    const int error = pthread_key_create(&key->native, NULL);
    if (error) {
        free(key);
        return result(RT_GUEST_TLS_HOST_ERROR, error, saved);
    }
    atomic_init(&key->bindings, 0);
    *out = key;
    return result(RT_GUEST_TLS_OK, 0, saved);
}

rt_guest_tls_result rt_guest_tls_get_candidate(const rt_guest_tls_key *key,
                                              rt_guest_tls_candidate *out)
{
    const int saved = errno;
    if (!key || !out) return result(RT_GUEST_TLS_INVALID_ARGUMENT, 0, saved);
    const uint64_t native = (uint64_t)key->native;
    if (sizeof(void *) != SLOT_BYTES || sizeof(pthread_key_t) != SLOT_BYTES
        || native < PROFILE_FIRST_KEY || native > PROFILE_LAST_KEY
        || native > (uint64_t)INT32_MAX / SLOT_BYTES)
        return result(RT_GUEST_TLS_UNREPRESENTABLE, 0, saved);
    *out = (rt_guest_tls_candidate){native, (uint32_t)(native * SLOT_BYTES)};
    return result(RT_GUEST_TLS_OK, 0, saved);
}

rt_guest_tls_result rt_guest_tls_bind(rt_guest_tls_key *key, void *tcb)
{
    const int saved = errno;
    if (!key || !tcb) return result(RT_GUEST_TLS_INVALID_ARGUMENT, 0, saved);
    void *previous = pthread_getspecific(key->native);
    const int error = pthread_setspecific(key->native, tcb);
    if (error) return result(RT_GUEST_TLS_HOST_ERROR, error, saved);
    if (!previous) atomic_fetch_add(&key->bindings, 1);
    return result(RT_GUEST_TLS_OK, 0, saved);
}

rt_guest_tls_result rt_guest_tls_read(const rt_guest_tls_key *key, void **out)
{
    const int saved = errno;
    if (!key || !out) return result(RT_GUEST_TLS_INVALID_ARGUMENT, 0, saved);
    *out = pthread_getspecific(key->native);
    return result(RT_GUEST_TLS_OK, 0, saved);
}

rt_guest_tls_result rt_guest_tls_clear(rt_guest_tls_key *key)
{
    const int saved = errno;
    if (!key) return result(RT_GUEST_TLS_INVALID_ARGUMENT, 0, saved);
    void *previous = pthread_getspecific(key->native);
    const int error = pthread_setspecific(key->native, NULL);
    if (error) return result(RT_GUEST_TLS_HOST_ERROR, error, saved);
    if (previous) atomic_fetch_sub(&key->bindings, 1);
    return result(RT_GUEST_TLS_OK, 0, saved);
}

rt_guest_tls_result rt_guest_tls_destroy(rt_guest_tls_key **out)
{
    const int saved = errno;
    if (!out) return result(RT_GUEST_TLS_INVALID_ARGUMENT, 0, saved);
    rt_guest_tls_key *key = *out;
    if (!key) return result(RT_GUEST_TLS_OK, 0, saved);
    if (atomic_load(&key->bindings)) return result(RT_GUEST_TLS_BUSY, 0, saved);
    const int error = pthread_key_delete(key->native);
    if (error) return result(RT_GUEST_TLS_HOST_ERROR, error, saved);
    free(key);
    *out = NULL;
    return result(RT_GUEST_TLS_OK, 0, saved);
}
