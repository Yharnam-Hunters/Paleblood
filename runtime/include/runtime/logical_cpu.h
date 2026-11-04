/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_LOGICAL_CPU_H
#define RUNTIME_LOGICAL_CPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Paleblood slot labels, not host CPU IDs or a measured console affinity set. No scheduling
   state is queried/set by slot init/read. Treat the value as immutable while published. There
   is no reconfiguration API; initialize separate storage for a different topology. */
typedef struct {
    uint32_t count;
    uint32_t reserved; /* zero: explicit validation, not implicit struct padding */
    uint64_t mask;
} rt_logical_cpu_slots;

typedef enum {
    RT_LOGICAL_CPU_OK,
    RT_LOGICAL_CPU_INVALID_ARGUMENT,
    RT_LOGICAL_CPU_COUNT_OUT_OF_RANGE,
    RT_LOGICAL_CPU_INVALID_STATE,
    RT_LOGICAL_CPU_UNSUPPORTED,
    RT_LOGICAL_CPU_HOST_ERROR,
    RT_LOGICAL_CPU_MALFORMED_HOST_DATA
} rt_logical_cpu_status;

typedef struct {
    rt_logical_cpu_status status;
    /* Exact errno captured after failed sysctlbyname, only for HOST_ERROR; otherwise zero.
       This is a native diagnostic, not a BSD/SCE error translation. It may itself be zero if
       the host reports failure without setting errno. All APIs restore caller errno. */
    int native_error;
} rt_logical_cpu_result;

/* Build a count1..64 value. Output must be valid aligned writable storage; no read of its old
   contents occurs. Failure leaves it unchanged. Do not initialize over an actively published
   value. Source and result storage for read must be disjoint. */
rt_logical_cpu_result rt_logical_cpu_slots_init(uint64_t count, rt_logical_cpu_slots *out);
/* Validate count/reserved/mask, then publish exactly8 bytes. Valid output may be unaligned;
   it must be writable and disjoint from slots. Failure leaves it untouched. */
rt_logical_cpu_result rt_logical_cpu_slots_read(const rt_logical_cpu_slots *slots, void *mask_out);

/* Darwin-only read of hw.logicalcpu, once per explicit caller request; no cache and no scheduling
   change. Validate RC0, exactly one int32 result, positive value <=64, then publish a uint32 count.
   A positive host count >64 is COUNT_OUT_OF_RANGE, not truncated. Zero/negative/incorrect size is
   MALFORMED_HOST_DATA. A non-Darwin build returns UNSUPPORTED without modifying output. This
   count describes host availability, not the calling thread's allowed CPU set or CPU identities.
   out must be valid aligned writable storage. NULL rejects before the host API is called. */
rt_logical_cpu_result rt_logical_cpu_darwin_count(uint32_t *out);

#ifdef __cplusplus
}
#endif
#endif
