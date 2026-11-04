/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(__APPLE__) || !defined(__MACH__)
#error This fake public-sysctl test requires the Darwin API declaration.
#endif
#include "runtime/logical_cpu.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)
static struct { int status, native_error; int32_t value; size_t size; unsigned calls; } fake;
static int owned_sysctl(const char *name, void *old, size_t *size, void *new_value, size_t new_size)
{
    CHECK(strcmp(name, "hw.logicalcpu") == 0 && old && size && *size == sizeof(int32_t));
    CHECK(!new_value && new_size == 0);
    fake.calls++;
    memcpy(old, &fake.value, sizeof fake.value);
    *size = fake.size;
    errno = fake.native_error;
    return fake.status;
}
#define sysctlbyname owned_sysctl
#include "../runtime/logical_cpu.c"
#undef sysctlbyname

static void query_is(rt_logical_cpu_status status, int native_error, uint32_t expected)
{
    struct guarded { uint32_t before, value, after; } g = {0x12345678,0xaabbccdd,0x87654321};
    errno = EDOM;
    const unsigned calls = fake.calls;
    rt_logical_cpu_result r = rt_logical_cpu_darwin_count(&g.value);
    CHECK(fake.calls == calls + 1 && r.status == status && r.native_error == native_error && errno == EDOM);
    CHECK(g.before == 0x12345678 && g.after == 0x87654321);
    CHECK(g.value == (status == RT_LOGICAL_CPU_OK ? expected : UINT32_C(0xaabbccdd)));
}
int main(void)
{
    fake.size = sizeof(int32_t); fake.value = 3; fake.native_error = EIO;
    errno = EDOM;
    rt_logical_cpu_result r = rt_logical_cpu_darwin_count(NULL);
    CHECK(r.status == RT_LOGICAL_CPU_INVALID_ARGUMENT && !r.native_error && errno == EDOM && !fake.calls);
    fake.status = -1;
    query_is(RT_LOGICAL_CPU_HOST_ERROR, EIO, 0);
    fake.native_error = 0;
    query_is(RT_LOGICAL_CPU_HOST_ERROR, 0, 0); /* Do not invent errno when a host failed without it. */
    fake.status = 1; fake.native_error = E2BIG;
    query_is(RT_LOGICAL_CPU_HOST_ERROR, E2BIG, 0);
    fake.status = 0; fake.native_error = ENOTTY;
    const size_t sizes[] = {0,1,3,5,8,SIZE_MAX};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        fake.size = sizes[i]; query_is(RT_LOGICAL_CPU_MALFORMED_HOST_DATA, 0, 0);
    }
    fake.size = sizeof(int32_t);
    const int32_t invalid[] = {0,-1,INT32_MIN};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        fake.value = invalid[i]; query_is(RT_LOGICAL_CPU_MALFORMED_HOST_DATA, 0, 0);
    }
    fake.value = 65; query_is(RT_LOGICAL_CPU_COUNT_OUT_OF_RANGE, 0, 0);
    fake.value = INT32_MAX; query_is(RT_LOGICAL_CPU_COUNT_OUT_OF_RANGE, 0, 0);
    const int32_t valid[] = {1,3,63,64};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        fake.value = valid[i]; query_is(RT_LOGICAL_CPU_OK, 0, (uint32_t)valid[i]);
    }
    /* Count reads have no connection to an already initialized immutable slot value. */
    fake.value = 3;
    uint32_t count = 0;
    CHECK(rt_logical_cpu_darwin_count(&count).status == RT_LOGICAL_CPU_OK && count == 3);
    rt_logical_cpu_slots slots;
    CHECK(rt_logical_cpu_slots_init(count, &slots).status == RT_LOGICAL_CPU_OK);
    fake.value = 7;
    CHECK(rt_logical_cpu_darwin_count(&count).status == RT_LOGICAL_CPU_OK && count == 7);
    const unsigned calls = fake.calls;
    uint64_t mask = 0;
    CHECK(rt_logical_cpu_slots_read(&slots, &mask).status == RT_LOGICAL_CPU_OK && mask == 7);
    CHECK(fake.calls == calls);
    printf("logical CPU query failures: failures=%d, mocked_calls=%u\n", failures, fake.calls);
    return failures != 0;
}
