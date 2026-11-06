/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_MODULE_TLS_H
#define RUNTIME_MODULE_TLS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_module_tls rt_module_tls;
typedef struct {
    uint64_t id;
    const void *initial; /* caller-readable filesz bytes; copied during creation */
    uint64_t filesz, memsz, alignment;
} rt_module_tls_spec;
typedef enum {
    RT_MODULE_TLS_OK,
    RT_MODULE_TLS_BAD_ARGUMENT,
    RT_MODULE_TLS_BAD_LAYOUT,
    RT_MODULE_TLS_NO_MEMORY,
    RT_MODULE_TLS_WRONG_THREAD,
    RT_MODULE_TLS_BAD_STATE,
    RT_MODULE_TLS_UNKNOWN_MODULE,
    RT_MODULE_TLS_BAD_OFFSET
} rt_module_tls_status;

/* Prepare every nonempty block eagerly on its owning host thread. IDs are nonzero,
   full-width and unique. Failure leaves *out untouched. No source pointer is retained. */
rt_module_tls_status rt_module_tls_create(const rt_module_tls_spec *specs, size_t count,
                                         rt_module_tls **out);
/* Before binding, replace one owned block with aligned, already initialized caller storage.
   Storage stays caller-owned. Each block can be borrowed once, without copying or zeroing it. */
rt_module_tls_status rt_module_tls_borrow(rt_module_tls *tls, uint64_t id, void *storage, uint64_t size);
/* One immutable context per host thread; binding seals the block layout permanently.
   No allocations occur during binding or lookup. */
rt_module_tls_status rt_module_tls_bind(rt_module_tls *tls);
rt_module_tls_status rt_module_tls_unbind(rt_module_tls *tls);
/* Offset == logical memsz returns the valid one-past address, without dereferencing it.
   Failure leaves *out untouched. IDs and offsets are never narrowed. */
rt_module_tls_status rt_module_tls_address(uint64_t id, uint64_t offset, void **out);
/* Only the owner may destroy an unbound context; borrowed storage is never released. */
rt_module_tls_status rt_module_tls_destroy(rt_module_tls *tls);
const char *rt_module_tls_status_message(rt_module_tls_status status);

#ifdef __cplusplus
}
#endif
#endif
