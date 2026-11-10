/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Unmapping, protecting and typing memory (runtime/syslib/kernel_memory.c): sceKernelMunmap,
   sceKernelMprotect and sceKernelGetDirectMemoryType, on maps of direct and flexible memory. */
#define _GNU_SOURCE
#include "../syslib_test.h"

#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef size_t (*f_size)(void);
typedef int32_t (*f_alloc)(int64_t, int64_t, uint64_t, uint64_t, int32_t, int64_t *);
typedef int32_t (*f_map_direct)(void **, uint64_t, int32_t, int32_t, int64_t, uint64_t);
typedef int32_t (*f_release)(int64_t, uint64_t);
typedef int32_t (*f_map_flexible)(void **, uint64_t, int32_t, int32_t);
typedef int32_t (*f_query)(const void *, void **, void **, int32_t *);
typedef int32_t (*f_munmap)(void *, uint64_t);
typedef int32_t (*f_mprotect)(const void *, uint64_t, int32_t);
typedef int32_t (*f_type)(int64_t, int32_t *, int64_t *, int64_t *);

enum { LENGTH = 0x10000, PAGE = 0x4000 };
static f_munmap unmap;
static f_mprotect protect;
static f_type type_of;
static f_query query;
static void *mapped;

/* Whether nothing is mapped at address any more: the host maps there again. */
static int is_unmapped(void *address)
{
    void *again = mmap(address, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (again != address) return 0;
    munmap(again, PAGE);
    return 1;
}

/* Whether a write to address faults (in a child, so the test goes on). */
static int write_faults(void *address)
{
    const pid_t child = fork();
    if (child == 0) {
        ((volatile char *)address)[0] = 1;
        _exit(0);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}

static void unmap_part(void) { unmap(mapped, PAGE); }
static void unmap_unknown(void) { static char local[PAGE]; unmap(local, sizeof local); }
static void query_after_unmap(void) { int32_t p; query(mapped, NULL, NULL, &p); }
static void protect_unknown_bits(void) { protect(mapped, LENGTH, 0x4); }
static void protect_part(void) { protect(mapped, PAGE, 1); }
static void type_unallocated(void) { int32_t t; int64_t s, e; type_of(-PAGE, &t, &s, &e); }
static int64_t allocated_offset;
static void type_no_variable(void) { int64_t s, e; type_of(allocated_offset, NULL, &s, &e); }
static void protect_unknown(void) { static char local[PAGE] __attribute__((aligned(PAGE))); protect(local, sizeof local, 1); }

SYSLIB_TEST(memory_unmap)
{
    const f_size pool_size = (f_size)fn("libkernel", "sceKernelGetDirectMemorySize");
    const f_alloc allocate = (f_alloc)fn("libkernel", "sceKernelAllocateDirectMemory");
    const f_map_direct map_direct = (f_map_direct)fn("libkernel", "sceKernelMapDirectMemory");
    const f_release release = (f_release)fn("libkernel", "sceKernelReleaseDirectMemory");
    const f_map_flexible map_flexible = (f_map_flexible)fn("libkernel", "sceKernelMapFlexibleMemory");
    query = (f_query)fn("libkernel", "sceKernelQueryMemoryProtection");
    unmap = (f_munmap)fn("libkernel", "sceKernelMunmap");
    protect = (f_mprotect)fn("libkernel", "sceKernelMprotect");
    type_of = (f_type)fn("libkernel", "sceKernelGetDirectMemoryType");
    CHECK(is_implemented("libkernel", "sceKernelMunmap") && is_implemented("libkernel", "sceKernelMprotect"));
    CHECK(is_implemented("libkernel", "sceKernelGetDirectMemoryType"));
    if (!pool_size || !allocate || !map_direct || !release || !map_flexible || !query || !unmap || !protect || !type_of) return;

    /* A write-combined (type 3) range: its type, start and end from any offset inside it. */
    int64_t start = -1, range_start = -1, range_end = -1;
    int32_t type = -1;
    CHECK(allocate(0, (int64_t)pool_size(), LENGTH, LENGTH, 3, &start) == 0 && start >= 0);
    CHECK(type_of(start + PAGE, &type, &range_start, &range_end) == 0);
    CHECK(type == 3 && range_start == start && range_end == start + LENGTH);
    allocated_offset = start;
    CHECK(died_in_fatal(type_no_variable));

    /* Protect a CPU map, then unmap it: the range can be released afterwards. */
    mapped = NULL;
    CHECK(map_direct(&mapped, LENGTH, 3, 0, start, LENGTH) == 0 && mapped);
    ((volatile char *)mapped)[0] = 7;
    int32_t protection = 0;
    CHECK(protect(mapped, LENGTH, 1) == 0 && query(mapped, NULL, NULL, &protection) == 0 && protection == 1);
    CHECK(((volatile char *)mapped)[0] == 7 && write_faults(mapped));
    CHECK(protect(mapped, LENGTH, 0x13) == 0 && query(mapped, NULL, NULL, &protection) == 0 && protection == 0x13);
    CHECK(protect(mapped, LENGTH, 3) == 0 && !write_faults(mapped));
    CHECK(died_in_fatal(protect_unknown_bits) && died_in_fatal(protect_part));
    CHECK(unmap(mapped, 0) == (int32_t)0x80020016u);
    CHECK(died_in_fatal(unmap_part));
    CHECK(unmap(mapped, LENGTH) == 0 && is_unmapped(mapped));
    CHECK(died_in_fatal(query_after_unmap));
    CHECK(release(start, LENGTH) == 0);

    /* A GPU-visible map goes at the range's GPU address; once unmapped, the range maps there again. */
    CHECK(allocate(0, (int64_t)pool_size(), LENGTH, LENGTH, 0, &start) == 0);
    void *gpu = NULL, *gpu_again = NULL;
    CHECK(map_direct(&gpu, LENGTH, 0x33, 0, start, LENGTH) == 0 && gpu);
    CHECK(unmap(gpu, LENGTH) == 0);
    CHECK(map_direct(&gpu_again, LENGTH, 0x33, 0, start, LENGTH) == 0 && gpu_again == gpu);
    CHECK(unmap(gpu_again, LENGTH) == 0 && release(start, LENGTH) == 0);

    /* Flexible memory unmaps the same way. */
    void *flexible = NULL;
    CHECK(map_flexible(&flexible, LENGTH, 3, 0) == 0 && flexible);
    CHECK(unmap(flexible, LENGTH) == 0 && is_unmapped(flexible));

    CHECK(died_in_fatal(unmap_unknown));
    CHECK(died_in_fatal(type_unallocated) && died_in_fatal(protect_unknown));
}
