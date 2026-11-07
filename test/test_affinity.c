/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Topology and public-registry tests with mocked providers; no host affinity query/change. */
#include "runtime/cpu.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

typedef struct {
    uint32_t ids[65];
    size_t count;
    rt_cpu_status status;
    unsigned calls;
} mock_cpu;

static rt_cpu_status query(void *context, uint32_t **ids, size_t *count)
{
    mock_cpu *mock = context;
    mock->calls++;
    *ids = NULL;
    *count = 0;
    if (mock->status != RT_CPU_OK) return mock->status;
    *ids = malloc(mock->count * sizeof **ids);
    if (!*ids && mock->count) return RT_CPU_NO_MEMORY;
    memcpy(*ids, mock->ids, mock->count * sizeof **ids);
    *count = mock->count;
    return RT_CPU_OK;
}

static void *lookup(const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, "libkernel") && !strcmp(e->symbol, symbol)) return e->fn;
    return NULL;
}

typedef int (*attr_one)(void **);
typedef int (*attr_copy)(void *, void **);
typedef int (*attr_affinity)(void **, void *);
static attr_copy copy_attr;
static void *self;
static void *attribute;
static mock_cpu main_cpus;

static void missing_cpu(void)
{
    main_cpus.ids[0] = 999999;
    main_cpus.count = 1;
    copy_attr(self, &attribute);
}

static void provider_failure(void)
{
    main_cpus.status = RT_CPU_QUERY_FAILED;
    copy_attr(self, &attribute);
}

static int aborts(void (*call)(void))
{
    const pid_t pid = fork();
    if (pid == 0) { call(); _exit(1); }
    int status;
    return pid > 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

int main(void)
{
    mock_cpu cpus = {.ids = {4097, 5, 130}, .count = 3};
    rt_cpu_topology topology;
    CHECK(rt_cpu_topology_init(&topology, query, &cpus, NULL) == RT_CPU_OK);
    CHECK(topology.count == 3 && topology.host[0] == 5 && topology.host[1] == 130 && topology.host[2] == 4097);
    uint64_t mask = 0;
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_OK && mask == 7);
    cpus.ids[0] = 4097; cpus.count = 1;
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_OK && mask == 4);
    cpus.ids[0] = 131;
    mask = UINT64_C(0xa1b2c3d4e5f60718);
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_UNREPRESENTABLE);
    CHECK(mask == UINT64_C(0xa1b2c3d4e5f60718));
    cpus.status = RT_CPU_QUERY_FAILED;
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_QUERY_FAILED);
    CHECK(mask == UINT64_C(0xa1b2c3d4e5f60718));

    cpus = (mock_cpu){.ids = {5, 4097, 130}, .count = 3};
    CHECK(rt_cpu_topology_init(&topology, query, &cpus, "4097,130,5") == RT_CPU_OK);
    cpus.ids[0] = 5; cpus.count = 1;
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_OK && mask == 4);
    const char *bad[] = {"", "5,", "5,5,130", "5,130", "5,130,999", "-1,5,130", "5, 130,4097", "4294967296,5,130"};
    cpus = (mock_cpu){.ids = {5, 4097, 130}, .count = 3};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        rt_cpu_topology saved = topology;
        CHECK(rt_cpu_topology_init(&topology, query, &cpus, bad[i]) == RT_CPU_BAD_CONFIGURATION);
        CHECK(memcmp(&topology, &saved, sizeof saved) == 0);
    }
    cpus = (mock_cpu){.count = 64};
    for (size_t i = 0; i < cpus.count; i++) cpus.ids[i] = 2000 + (uint32_t)i * 17;
    CHECK(rt_cpu_topology_init(&topology, query, &cpus, NULL) == RT_CPU_OK);
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_OK && mask == UINT64_MAX);
    cpus.ids[0] = 2000 + 63 * 17; cpus.count = 1;
    CHECK(rt_cpu_topology_read(&topology, &mask) == RT_CPU_OK && mask == (UINT64_C(1) << 63));
    cpus.count = 65;
    for (size_t i = 0; i < cpus.count; i++) cpus.ids[i] = (uint32_t)i;
    CHECK(rt_cpu_topology_init(&topology, query, &cpus, NULL) == RT_CPU_UNREPRESENTABLE);
    cpus = (mock_cpu){.ids = {5, 5}, .count = 2};
    CHECK(rt_cpu_topology_init(&topology, query, &cpus, NULL) == RT_CPU_BAD_CONFIGURATION);

    /* The public registry path uses only the injected current-main provider. */
    main_cpus = (mock_cpu){.ids = {4097, 5, 130}, .count = 3};
    unsigned char stack[32];
    CHECK(rt_thread_register_main_cpu(stack, sizeof stack, query, &main_cpus, NULL) == RT_CPU_OK);
    void *(*get_self)(void) = (void *(*)(void))lookup("scePthreadSelf");
    const attr_one init = (attr_one)lookup("scePthreadAttrInit");
    const attr_one destroy = (attr_one)lookup("scePthreadAttrDestroy");
    copy_attr = (attr_copy)lookup("scePthreadAttrGet");
    const attr_affinity get = (attr_affinity)lookup("scePthreadAttrGetaffinity");
    CHECK(get_self && init && destroy && copy_attr && get);
    if (failures) return 1;
    self = get_self();
    CHECK(self);
    CHECK(init(&attribute) == 0 && attribute);
    unsigned char output[10], poison[10];
    memset(poison, 0xa5, sizeof poison); memcpy(output, poison, sizeof output);
    const unsigned calls = main_cpus.calls;
    CHECK(get(&attribute, output + 1) == 0);
    memcpy(&mask, output + 1, sizeof mask);
    CHECK(mask == UINT64_MAX && output[0] == 0xa5 && output[9] == 0xa5);
    CHECK(main_cpus.calls == calls); /* fresh unrestricted is not a host affinity snapshot */
    CHECK(copy_attr(self, &attribute) == 0);
    CHECK(get(&attribute, output + 1) == 0);
    memcpy(&mask, output + 1, sizeof mask);
    CHECK(mask == 7 && output[0] == 0xa5 && output[9] == 0xa5);
    main_cpus.ids[0] = 130; main_cpus.count = 1;
    CHECK(get(&attribute, &mask) == 0 && mask == 7); /* copied attribute remains a snapshot */
    CHECK(copy_attr(self, &attribute) == 0 && get(&attribute, &mask) == 0 && mask == 2);

    /* Publication may overlap the pointer variable; consume it before the store, then restore. */
    void *saved = attribute;
    CHECK(get(&attribute, &attribute) == 0);
    memcpy(&mask, &attribute, sizeof mask);
    attribute = saved;
    CHECK(mask == 2);
    memcpy(output, poison, sizeof output);
    void *empty = NULL;
    CHECK(get(NULL, output + 1) == (int)0x80020016);
    CHECK(memcmp(output, poison, sizeof output) == 0);
    CHECK(get(&empty, output + 1) == (int)0x80020016);
    CHECK(memcmp(output, poison, sizeof output) == 0);
    CHECK(copy_attr(NULL, &attribute) == (int)0x80020016);
    CHECK(get(&attribute, &mask) == 0 && mask == 2);
    CHECK(aborts(missing_cpu));
    CHECK(aborts(provider_failure));
    /* Keep the upstream setter and verify that explicit attributes retain its mask. */
    int (*set)(void **, uint64_t) = (int (*)(void **, uint64_t))lookup("scePthreadAttrSetaffinity");
    CHECK(set && set(&attribute, 5) == 0 && get(&attribute, &mask) == 0 && mask == 5);
    CHECK((uintptr_t)self < (UINT64_C(1) << 40)); /* upstream low-address thread identity */
    CHECK(destroy(&attribute) == 0 && !attribute);
    CHECK(rt_thread_register_main_cpu(stack, sizeof stack, query, &main_cpus, NULL) == RT_CPU_BAD_CONFIGURATION);
    if (!failures) puts("affinity: mocked provider ok");
    return failures != 0;
}
