/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GUEST_TLS_H
#define RUNTIME_GUEST_TLS_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_guest_tls_key rt_guest_tls_key;
typedef enum {
    RT_GUEST_TLS_OK,
    RT_GUEST_TLS_INVALID_ARGUMENT,
    RT_GUEST_TLS_NO_MEMORY,
    RT_GUEST_TLS_UNREPRESENTABLE,
    RT_GUEST_TLS_HOST_ERROR,
    RT_GUEST_TLS_BUSY
} rt_guest_tls_status;
typedef struct {
    rt_guest_tls_status status;
    int native_error; /* Exact pthread error on HOST_ERROR; zero for other statuses. */
} rt_guest_tls_result;
typedef struct {
    uint64_t native_key;       /* Owned public key identity; read-only inspection only. */
    uint32_t candidate_offset; /* Checked key*8, not proof of the private GS layout. */
} rt_guest_tls_candidate;

/* Own a public pthread key so guest-pointer storage never replaces the host TCB.
   Darwin x86-64 only; *out must be NULL. Failures preserve it and every API preserves errno.
   Lifecycle and layout qualification are described in docs/darwin-guest-tls.md. */
rt_guest_tls_result rt_guest_tls_create(rt_guest_tls_key **out);

/* Numeric pthread keys are not a public GS layout guarantee. This only bounds a candidate;
   startup must qualify direct/public agreement on each thread before using the offset.
   Rejection leaves *out unchanged. The library itself never reads GS. */
rt_guest_tls_result rt_guest_tls_get_candidate(const rt_guest_tls_key *key,
                                              rt_guest_tls_candidate *out);

/* Per-thread slots isolate bindings; they do not synchronize TCB contents or own its lifetime.
   Keep the non-NULL TCB alive until clear succeeds and every reader has stopped. */
rt_guest_tls_result rt_guest_tls_bind(rt_guest_tls_key *key, void *tcb);
/* Public getspecific on this thread; unbound success stores NULL. Failure preserves *out. */
rt_guest_tls_result rt_guest_tls_read(const rt_guest_tls_key *key, void **out);
/* Clear after this thread's readers stop, before exit/cancellation or freeing its TCB.
   An unbound clear is idempotent. */
rt_guest_tls_result rt_guest_tls_clear(rt_guest_tls_key *key);

/* No destructor guesses when guest readers are done: remaining bindings keep the key BUSY.
   Quiesce all API/direct readers first. Success clears *key; failure preserves ownership.
   External setspecific/key_delete would bypass accounting and are forbidden. */
rt_guest_tls_result rt_guest_tls_destroy(rt_guest_tls_key **key);

#ifdef __cplusplus
}
#endif
#endif
