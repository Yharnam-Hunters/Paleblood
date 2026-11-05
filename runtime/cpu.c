/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "runtime/cpu.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#ifdef __linux__
#include <sched.h>
#endif

static int compare_id(const void *left, const void *right)
{
    const uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static int unique_ids(const uint32_t *ids, size_t count)
{
    if (!ids || !count || count > RT_CPU_MAX_GUEST) return 0;
    for (size_t i = 0; i < count; i++)
        for (size_t j = 0; j < i; j++)
            if (ids[i] == ids[j]) return 0;
    return 1;
}

static int parse_mapping(const char *text, uint32_t *ids, size_t *count)
{
    size_t n = 0;
    const char *p = text;
    while (*p) {
        if (n == RT_CPU_MAX_GUEST || *p < '0' || *p > '9') return 0;
        uint64_t id = 0;
        do {
            id = id * 10 + (unsigned)(*p++ - '0');
            if (id > UINT32_MAX) return 0;
        } while (*p >= '0' && *p <= '9');
        ids[n++] = (uint32_t)id;
        if (!*p) break;
        if (*p++ != ',' || !*p) return 0;
    }
    *count = n;
    return unique_ids(ids, n);
}

rt_cpu_status rt_cpu_topology_init(rt_cpu_topology *topology, rt_cpu_query query, void *context,
                                   const char *mapping)
{
    if (!topology || !query) return RT_CPU_BAD_CONFIGURATION;
    uint32_t *allowed = NULL;
    size_t count = 0;
    rt_cpu_status status = query(context, &allowed, &count);
    if (status != RT_CPU_OK) return status;
    if (!unique_ids(allowed, count)) {
        free(allowed);
        return count > RT_CPU_MAX_GUEST ? RT_CPU_UNREPRESENTABLE : RT_CPU_BAD_CONFIGURATION;
    }
    rt_cpu_topology next = {.query = query, .context = context, .count = count};
    if (mapping) {
        size_t mapped = 0;
        if (!parse_mapping(mapping, next.host, &mapped) || mapped != count) {
            free(allowed);
            return RT_CPU_BAD_CONFIGURATION;
        }
        for (size_t i = 0; i < count; i++) {
            size_t j = 0;
            while (j < count && allowed[j] != next.host[i]) j++;
            if (j == count) {
                free(allowed);
                return RT_CPU_BAD_CONFIGURATION;
            }
        }
    } else {
        for (size_t i = 0; i < count; i++) next.host[i] = allowed[i];
        qsort(next.host, count, sizeof next.host[0], compare_id);
    }
    free(allowed);
    *topology = next;
    return RT_CPU_OK;
}

rt_cpu_status rt_cpu_topology_read(const rt_cpu_topology *topology, uint64_t *mask)
{
    if (!topology || !mask || !topology->query || !unique_ids(topology->host, topology->count))
        return RT_CPU_BAD_CONFIGURATION;
    uint32_t *allowed = NULL;
    size_t count = 0;
    rt_cpu_status status = topology->query(topology->context, &allowed, &count);
    if (status != RT_CPU_OK) return status;
    if (!unique_ids(allowed, count)) {
        free(allowed);
        return count > RT_CPU_MAX_GUEST ? RT_CPU_UNREPRESENTABLE : RT_CPU_BAD_CONFIGURATION;
    }
    uint64_t result = 0;
    for (size_t i = 0; i < count; i++) {
        size_t bit = 0;
        while (bit < topology->count && topology->host[bit] != allowed[i]) bit++;
        if (bit == topology->count) {
            free(allowed);
            return RT_CPU_UNREPRESENTABLE;
        }
        result |= UINT64_C(1) << bit;
    }
    free(allowed);
    *mask = result;
    return RT_CPU_OK;
}

rt_cpu_status rt_cpu_query_native(void *context, uint32_t **ids, size_t *count)
{
    (void)context;
    *ids = NULL;
    *count = 0;
#ifdef __linux__
    /* A bounded dynamic set avoids CPU_SETSIZE and sparse/high host-ID truncation. If the host
       needs more than this query bound, fail clearly; never guess which CPUs it allowed. */
    for (size_t bits = 128; bits <= (1u << 20); bits *= 2) {
        const size_t bytes = CPU_ALLOC_SIZE(bits);
        cpu_set_t *set = CPU_ALLOC(bits);
        if (!set) return RT_CPU_NO_MEMORY;
        CPU_ZERO_S(bytes, set);
        if (sched_getaffinity(0, bytes, set) != 0) {
            const int error = errno;
            CPU_FREE(set);
            if (error == EINVAL) continue;
            return RT_CPU_QUERY_FAILED;
        }
        const int n = CPU_COUNT_S(bytes, set);
        if (n < 1 || n > RT_CPU_MAX_GUEST) {
            CPU_FREE(set);
            return RT_CPU_UNREPRESENTABLE;
        }
        uint32_t *found = malloc((size_t)n * sizeof *found);
        if (!found) { CPU_FREE(set); return RT_CPU_NO_MEMORY; }
        size_t at = 0;
        for (size_t id = 0; id < bits; id++)
            if (CPU_ISSET_S(id, bytes, set)) found[at++] = (uint32_t)id;
        CPU_FREE(set);
        if (at != (size_t)n) { free(found); return RT_CPU_QUERY_FAILED; }
        *ids = found;
        *count = at;
        return RT_CPU_OK;
    }
#endif
    return RT_CPU_QUERY_FAILED;
}

const char *rt_cpu_status_message(rt_cpu_status status)
{
    switch (status) {
    case RT_CPU_OK: return "CPU topology available";
    case RT_CPU_QUERY_FAILED: return "cannot query actual host-thread affinity within the supported backend";
    case RT_CPU_NO_MEMORY: return "cannot allocate host affinity query storage";
    case RT_CPU_UNREPRESENTABLE: return "host affinity cannot be represented by the fixed guest CPU map (maximum64); no CPUs discarded";
    case RT_CPU_BAD_CONFIGURATION: return "invalid CPU map/provider: require unique allowed host IDs and a full decimal permutation";
    }
    return "unknown CPU topology failure";
}
