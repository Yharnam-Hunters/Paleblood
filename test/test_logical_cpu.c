/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/logical_cpu.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)
_Static_assert(sizeof(rt_logical_cpu_slots) == 16, "owned value size");
_Static_assert(offsetof(rt_logical_cpu_slots, mask) == 8, "owned named mask field");

static void result_is(rt_logical_cpu_result r, rt_logical_cpu_status status)
{
    CHECK(r.status == status && r.native_error == 0 && errno == ERANGE);
}
int main(void)
{
    struct guarded { uint64_t before; rt_logical_cpu_slots slots; uint64_t after; } g, original;
    memset(&g, 0xa5, sizeof g); original = g;
    errno = ERANGE;
    const uint64_t bad[] = {0, 65, UINT32_MAX, UINT64_MAX};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        result_is(rt_logical_cpu_slots_init(bad[i], &g.slots), RT_LOGICAL_CPU_COUNT_OUT_OF_RANGE);
        CHECK(memcmp(&g, &original, sizeof g) == 0);
    }
    result_is(rt_logical_cpu_slots_init(1, NULL), RT_LOGICAL_CPU_INVALID_ARGUMENT);
    const uint32_t counts[] = {1, 3, 63, 64};
    const uint64_t masks[] = {1, 7, UINT64_MAX >> 1, UINT64_MAX};
    for (size_t n = 0; n < sizeof counts / sizeof counts[0]; n++) {
        g = original;
        result_is(rt_logical_cpu_slots_init(counts[n], &g.slots), RT_LOGICAL_CPU_OK);
        CHECK(g.before == original.before && g.after == original.after);
        CHECK(g.slots.count == counts[n] && g.slots.reserved == 0 && g.slots.mask == masks[n]);
        _Alignas(uint64_t) unsigned char output[17];
        unsigned char expected[17];
        CHECK((uintptr_t)(output + 1) % sizeof(uint64_t) != 0);
        memset(output, 0x3c, sizeof output); memcpy(expected, output, sizeof output);
        memcpy(expected + 1, &masks[n], 8);
        const rt_logical_cpu_slots saved = g.slots;
        result_is(rt_logical_cpu_slots_read(&g.slots, output + 1), RT_LOGICAL_CPU_OK);
        CHECK(memcmp(output, expected, sizeof output) == 0 && memcmp(&saved, &g.slots, sizeof saved) == 0);
        for (unsigned repeat = 0; repeat < 3; repeat++) {
            uint64_t read = 0;
            result_is(rt_logical_cpu_slots_read(&g.slots, &read), RT_LOGICAL_CPU_OK);
            CHECK(read == masks[n]);
        }
    }
    const rt_logical_cpu_slots invalid[] = {{0,0,0},{65,0,UINT64_MAX},{3,1,7},{3,0,3},{64,0,UINT64_MAX-1},{UINT32_MAX,0,0}};
    uint64_t mask = UINT64_C(0x1122334455667788);
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        result_is(rt_logical_cpu_slots_read(&invalid[i], &mask), RT_LOGICAL_CPU_INVALID_STATE);
        CHECK(mask == UINT64_C(0x1122334455667788));
    }
    result_is(rt_logical_cpu_slots_read(NULL, &mask), RT_LOGICAL_CPU_INVALID_ARGUMENT);
    result_is(rt_logical_cpu_slots_read(&g.slots, NULL), RT_LOGICAL_CPU_INVALID_ARGUMENT);
    CHECK(mask == UINT64_C(0x1122334455667788));
#if !defined(__APPLE__) || !defined(__MACH__)
    uint32_t count = UINT32_C(0xaabbccdd);
    result_is(rt_logical_cpu_darwin_count(&count), RT_LOGICAL_CPU_UNSUPPORTED);
    CHECK(count == UINT32_C(0xaabbccdd));
#endif
    result_is(rt_logical_cpu_darwin_count(NULL), RT_LOGICAL_CPU_INVALID_ARGUMENT);
    printf("logical CPU slots: failures=%d\n", failures);
    return failures != 0;
}
