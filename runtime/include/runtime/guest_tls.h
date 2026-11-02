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

/* Darwin x86-64 only. Create one owned public pthread key with no destructor. *out must be
   NULL; failure leaves it unchanged. No GS read/write, host TCB allocation or signal handler
   occurs here. All functions preserve caller errno and report pthread errors separately.
   Caller-provided output storage must be valid and disjoint from the opaque handle and TCBs. */
rt_guest_tls_result rt_guest_tls_create(rt_guest_tls_key **out);

/* Candidate metadata for the inspected 64-bit public-key profile 256..767. Reject a key/width
   outside that profile or a displacement above INT32_MAX, leaving *out unchanged. This is
   not a public platform ABI guarantee or a compatibility test: an explicit owned startup/test
   boundary must establish GS:candidate_offset == public getspecific on every participating
   thread before patched guest code may use it. The library never performs that raw read. */
rt_guest_tls_result rt_guest_tls_get_candidate(const rt_guest_tls_key *key,
                                              rt_guest_tls_candidate *out);

/* Bind a non-NULL caller-owned live TCB pointer on the calling thread. Rebinding changes only
   this thread's public slot; this API does not publish TCB contents between threads. Caller
   must keep each TCB alive until every direct/API reader using it has finished. Other threads
   may bind/read/clear concurrently. */
rt_guest_tls_result rt_guest_tls_bind(rt_guest_tls_key *key, void *tcb);
/* Public getspecific on this thread; unbound success stores NULL. Failure preserves *out. */
rt_guest_tls_result rt_guest_tls_read(const rt_guest_tls_key *key, void **out);
/* Clear only after this thread has stopped all direct/API reads; idempotent when unbound.
   Must succeed before thread exit/cancellation and before freeing the former TCB. */
rt_guest_tls_result rt_guest_tls_clear(rt_guest_tls_key *key);

/* Exclusive lifecycle operation: caller must quiesce every other API call/direct reader before
   calling, including metadata readers. A remaining binding rejects with BUSY; a thread that
   exits without clearing conservatively leaves the object BUSY. No destructor guesses cleanup.
   On success delete the public key, free this handle and set *key=NULL. NULL *key is idempotent.
   On failure keep it owned/unchanged. External setspecific/key_delete on native_key is forbidden;
   external getspecific is permitted while the handle/key remains alive. Destruction cannot
   detect a raw reader violating the clear/quiescence contract. */
rt_guest_tls_result rt_guest_tls_destroy(rt_guest_tls_key **key);

#ifdef __cplusplus
}
#endif
#endif
