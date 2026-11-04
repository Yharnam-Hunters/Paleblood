/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/logical_cpu.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

static rt_logical_cpu_result result(rt_logical_cpu_status status, int native_error, int saved_errno)
{
    errno = saved_errno;
    return (rt_logical_cpu_result){status, native_error};
}

static uint64_t slot_mask(uint32_t count)
{
    /* Only call after validating1..64; never shift a 64-bit integer by64. */
    return count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
}

rt_logical_cpu_result rt_logical_cpu_slots_init(uint64_t count, rt_logical_cpu_slots *out)
{
    const int saved = errno;
    if (!out) return result(RT_LOGICAL_CPU_INVALID_ARGUMENT, 0, saved);
    if (!count || count > 64) return result(RT_LOGICAL_CPU_COUNT_OUT_OF_RANGE, 0, saved);
    const rt_logical_cpu_slots value = {(uint32_t)count, 0, slot_mask((uint32_t)count)};
    *out = value;
    return result(RT_LOGICAL_CPU_OK, 0, saved);
}

rt_logical_cpu_result rt_logical_cpu_slots_read(const rt_logical_cpu_slots *slots, void *mask_out)
{
    const int saved = errno;
    if (!slots || !mask_out) return result(RT_LOGICAL_CPU_INVALID_ARGUMENT, 0, saved);
    const rt_logical_cpu_slots value = *slots;
    if (!value.count || value.count > 64 || value.reserved || value.mask != slot_mask(value.count))
        return result(RT_LOGICAL_CPU_INVALID_STATE, 0, saved);
    memcpy(mask_out, &value.mask, sizeof value.mask);
    return result(RT_LOGICAL_CPU_OK, 0, saved);
}

rt_logical_cpu_result rt_logical_cpu_darwin_count(uint32_t *out)
{
    const int saved = errno;
    if (!out) return result(RT_LOGICAL_CPU_INVALID_ARGUMENT, 0, saved);
#if defined(__APPLE__) && defined(__MACH__)
    int32_t count = 0;
    size_t size = sizeof count;
    errno = 0;
    const int status = sysctlbyname("hw.logicalcpu", &count, &size, NULL, 0);
    const int native_error = errno;
    if (status != 0) return result(RT_LOGICAL_CPU_HOST_ERROR, native_error, saved);
    if (size != sizeof count || count <= 0)
        return result(RT_LOGICAL_CPU_MALFORMED_HOST_DATA, 0, saved);
    if (count > 64) return result(RT_LOGICAL_CPU_COUNT_OUT_OF_RANGE, 0, saved);
    *out = (uint32_t)count;
    return result(RT_LOGICAL_CPU_OK, 0, saved);
#else
    return result(RT_LOGICAL_CPU_UNSUPPORTED, 0, saved);
#endif
}
