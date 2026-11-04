/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One ordinary owned component test; no guest code, hard-affinity read or scheduling changes. */
#include "runtime/logical_cpu.h"
#include <errno.h>
#include <stdio.h>

int main(void)
{
    struct { uint32_t before, count, after; } output = {0x11223344,0xaabbccdd,0x55667788};
    errno = EDOM;
    const rt_logical_cpu_result r = rt_logical_cpu_darwin_count(&output.count);
    if (r.status != RT_LOGICAL_CPU_OK || r.native_error || errno != EDOM
        || !output.count || output.count > 64 || output.before != 0x11223344 || output.after != 0x55667788) {
        fprintf(stderr, "owned logical count failed: status=%d native_error=%d\n", (int)r.status, r.native_error);
        return 1;
    }
    rt_logical_cpu_slots slots;
    uint64_t mask = 0;
    if (rt_logical_cpu_slots_init(output.count, &slots).status != RT_LOGICAL_CPU_OK
        || rt_logical_cpu_slots_read(&slots, &mask).status != RT_LOGICAL_CPU_OK
        || slots.count != output.count || mask != slots.mask || errno != EDOM)
        return 1;
    printf("owned hw.logicalcpu count=%u; logical unpinned slots only; no host-ID/affinity claim\n", output.count);
    return 0;
}
