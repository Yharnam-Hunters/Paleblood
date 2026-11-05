/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
typedef int32_t (*allocate_fn)(int64_t, int64_t, uint64_t, uint64_t, int32_t, int64_t *);
typedef int32_t (*map_fn)(void **, uint64_t, int32_t, int32_t, int64_t, uint64_t);
static allocate_fn allocate_memory;
static map_fn map_memory;
enum { PAGE_BYTES = 0x4000, CPU_READ_WRITE = 3 };
static const char *expected_function, *expected_reason;
static int64_t output_offset;

static void *entry(const char *symbol)
{
    for (const rt_syslib_entry *p = rt_syslib_begin(); p != rt_syslib_end(); ++p)
        if (!strcmp(p->library, "libkernel") && !strcmp(p->symbol, symbol)) return p->fn;
    return NULL;
}

static void observe_stop(const char *function, const char *reason)
{
    /* Check the diagnostic before mmap can reject an accidentally admitted range. */
    const int matched = !strcmp(function, expected_function) && strstr(reason, expected_reason)
                        && output_offset == -1;
    _exit(matched ? 0 : 1);
}
static void rejects(void (*call)(void), const char *function, const char *reason)
{
    const pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        output_offset = -1;
        expected_function = function;
        expected_reason = reason;
        rt_fatal_observer = observe_stop;
        call();
        _exit(1);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void allocation_past_end(void)
{
    allocate_memory(4 * PAGE_BYTES, 5 * PAGE_BYTES, 2 * PAGE_BYTES, PAGE_BYTES, 0, &output_offset);
}
static void allocation_wrapping_end(void)
{
    allocate_memory(4 * PAGE_BYTES, 5 * PAGE_BYTES, UINT64_MAX - PAGE_BYTES + 1, PAGE_BYTES, 0, &output_offset);
}
static void mapping_past_allocation(void)
{
    void *address = NULL;
    map_memory(&address, 2 * PAGE_BYTES, CPU_READ_WRITE, 0, 4 * PAGE_BYTES, PAGE_BYTES);
}
static void mapping_after_allocation(void)
{
    void *address = NULL;
    map_memory(&address, PAGE_BYTES, CPU_READ_WRITE, 0, 6 * PAGE_BYTES, PAGE_BYTES);
}
static void mapping_wrapping_end(void)
{
    void *address = NULL;
    map_memory(&address, UINT64_MAX - PAGE_BYTES + 1, CPU_READ_WRITE, 0, 4 * PAGE_BYTES, PAGE_BYTES);
}
static void mapping_misaligned_hint(void)
{
    void *address = (void *)(uintptr_t)0xfd00004000;
    map_memory(&address, PAGE_BYTES, CPU_READ_WRITE, 0, 4 * PAGE_BYTES, 2 * PAGE_BYTES);
}
int main(void)
{
    allocate_memory = (allocate_fn)entry("sceKernelAllocateDirectMemory");
    map_memory = (map_fn)entry("sceKernelMapDirectMemory");
    CHECK(allocate_memory && map_memory);
    int64_t offset = -1;
    CHECK(!allocate_memory(4 * PAGE_BYTES, 5 * PAGE_BYTES, PAGE_BYTES, PAGE_BYTES, 0, &offset));
    CHECK(offset == 4 * PAGE_BYTES);
    rejects(allocation_past_end, "sceKernelAllocateDirectMemory", "no room in the search range");
    rejects(allocation_wrapping_end, "sceKernelAllocateDirectMemory", "no room in the search range");
    rejects(mapping_past_allocation, "sceKernelMapDirectMemory", "range that was not allocated");
    rejects(mapping_after_allocation, "sceKernelMapDirectMemory", "range that was not allocated");
    rejects(mapping_wrapping_end, "sceKernelMapDirectMemory", "range that was not allocated");
    rejects(mapping_misaligned_hint, "sceKernelMapDirectMemory", "address hint off the alignment");
    /* Search starts inside [5P, 7P): the next fit must be 7P, never the wrapping gap. */
    CHECK(!allocate_memory(5 * PAGE_BYTES, 7 * PAGE_BYTES, 2 * PAGE_BYTES, PAGE_BYTES, 0, &offset));
    CHECK(offset == 5 * PAGE_BYTES);
    CHECK(!allocate_memory(6 * PAGE_BYTES, 8 * PAGE_BYTES, PAGE_BYTES, PAGE_BYTES, 0, &offset));
    CHECK(offset == 7 * PAGE_BYTES);
    offset = 4 * PAGE_BYTES;
    void *address = NULL;
    CHECK(!map_memory(&address, PAGE_BYTES, CPU_READ_WRITE, 0, offset, PAGE_BYTES));
    CHECK(address && (uintptr_t)address % PAGE_BYTES == 0);
    ((volatile unsigned char *)address)[PAGE_BYTES - 1] = 0x5a;
    CHECK(((volatile unsigned char *)address)[PAGE_BYTES - 1] == 0x5a);
    return 0;
}
