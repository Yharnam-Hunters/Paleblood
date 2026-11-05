/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_CPU_H
#define RUNTIME_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Own-runtime policy: at most64 guest CPU bits, mapped to host CPU identifiers. The numbers
   identify this port's topology; they are not observations of a console's default affinity. */
enum { RT_CPU_MAX_GUEST = 64 };
typedef enum {
    RT_CPU_OK,
    RT_CPU_QUERY_FAILED,
    RT_CPU_NO_MEMORY,
    RT_CPU_UNREPRESENTABLE,
    RT_CPU_BAD_CONFIGURATION
} rt_cpu_status;

/* On success return a nonempty, malloc-owned array of unique allowed host CPU IDs; caller frees
   it. On failure leave *ids NULL. The provider queries the calling registered host thread. */
typedef rt_cpu_status (*rt_cpu_query)(void *context, uint32_t **ids, size_t *count);

typedef struct {
    uint32_t host[RT_CPU_MAX_GUEST];
    size_t count;
    rt_cpu_query query;
    void *context;
} rt_cpu_topology;

/* mapping NULL: all allowed host IDs in ascending order become guest bits0..N-1. An explicit
   decimal comma-list is an ordered permutation of the same full set, never a hidden subset.
   Initialization does not alter host scheduling. Failure leaves *topology unchanged. */
rt_cpu_status rt_cpu_topology_init(rt_cpu_topology *topology, rt_cpu_query query, void *context,
                                   const char *mapping);

/* Re-query actual allowed CPUs. A subset retains its original bit positions; a newly allowed
   host ID outside the fixed mapping fails instead of being discarded. Failure leaves *mask. */
rt_cpu_status rt_cpu_topology_read(const rt_cpu_topology *topology, uint64_t *mask);

/* Linux backend with a dynamically sized kernel CPU set. Other hosts fail explicitly. */
rt_cpu_status rt_cpu_query_native(void *context, uint32_t **ids, size_t *count);
const char *rt_cpu_status_message(rt_cpu_status status);

#ifdef __cplusplus
}
#endif

#endif
