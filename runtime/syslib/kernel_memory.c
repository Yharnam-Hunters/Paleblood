/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Direct memory (libkernel): the pool of physical
   memory a title allocates ranges of and maps into its address space; and flexible memory, which
   the system maps for a title without a range of that pool (sceKernelMapFlexibleMemory, at the
   end of this file).

   Facts used:
   - The signatures: size_t sceKernelGetDirectMemorySize(void); int32_t
     sceKernelAllocateDirectMemory(off_t search_start, off_t search_end, size_t length, size_t
     alignment, int32_t memory_type, off_t *start); int32_t sceKernelMapDirectMemory(void **address,
     size_t length, int32_t protection, int32_t flags, off_t start, size_t alignment); int32_t
     sceKernelReleaseDirectMemory(off_t start, size_t length). The OpenOrbis homebrew SDK header
     (include/orbis/libkernel.h) declares them so; it corroborates the shapes, not the behaviour.
   - Protection bits are FreeBSD's VM_PROT_READ (1) and VM_PROT_WRITE (2) for the CPU (the same
     header names them ORBIS_KERNEL_PROT_CPU_READ/WRITE) and 0x10 and 0x20 for the GPU
     (ORBIS_KERNEL_PROT_GPU_READ/WRITE); memory type 0 is write-back memory (ORBIS_KERNEL_WB_ONION
     there). The executable maps with protection 3 (CPU read/write) and 0x33 (CPU and GPU
     read/write).
   - A title's own first use (an executable's startup heap) asks for the size, allocates a range
     below it with an alignment, maps it read/write at an address the system picks (flags 0), and
     releases the range if the map fails.

   The pool is a host memory file, created on first use: an allocation is a range of it, a map is a
   host mapping of that range, so two maps of one range see the same bytes, as physical memory
   does. Its size is this runtime's choice (RT_DIRECT_MEMORY_SIZE), not a measured PS4 value. Every
   case whose PS4 answer is not established stops by the function's name instead of guessing: map
   flags, an address hint, protection beyond CPU and GPU read/write, a range that does not fit, releasing
   part of an allocation or a mapped one, and the error values. */
#define _GNU_SOURCE
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

/* The pool this runtime provides, and what sceKernelGetDirectMemorySize answers: a choice of the
   runtime, not the PS4's value (which is not established here). It is large, and sparse on the host
   (a memory file only takes host memory for the pages written). */
#define RT_DIRECT_MEMORY_SIZE (5ull << 30)
/* The granularity this runtime accepts for lengths, starts and alignments: 16 KiB, which every
   call the executable makes respects. The PS4's own rule is not established here (see getpagesize
   in docs/runtime-syscalls.md), so anything finer stops by name instead of being rounded. */
#define DIRECT_PAGE 0x4000ull

enum { PROT_CPU_READ = 0x1, PROT_CPU_WRITE = 0x2, PROT_GPU_READ = 0x10, PROT_GPU_WRITE = 0x20 };
enum { MEMORY_TYPE_WB_ONION = 0x0, MEMORY_TYPE_WC_GARLIC = 0x3, MEMORY_TYPE_WB_GARLIC = 0xa };
enum { MAX_RANGES = 1024 };

typedef struct {
    uint64_t start, length;
    int32_t memory_type;
    int maps; /* how many host mappings of the range exist */
} direct_range;

static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static int pool_fd = -1;
static direct_range ranges[MAX_RANGES]; /* allocated ranges, ascending by start */
static int range_count;


static int is_page_multiple(uint64_t value) { return value % DIRECT_PAGE == 0; }
static int is_power_of_two(uint64_t value) { return value && !(value & (value - 1)); }

/* The pool's file, created the first time a range is allocated. */
static int pool(const char *caller)
{
    if (pool_fd >= 0) return pool_fd;
    const int fd = memfd_create("direct-memory", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)RT_DIRECT_MEMORY_SIZE) != 0)
        rt_fatal(caller, "cannot create the host file that holds direct memory");
    pool_fd = fd;
    return pool_fd;
}

static size_t get_direct_memory_size(void) { return (size_t)RT_DIRECT_MEMORY_SIZE; }

static uint64_t align_up(uint64_t value, uint64_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

/* The lowest aligned start at or above `from` where `length` bytes fit below `limit` without
   touching an allocated range (ranges are kept ascending); *found says whether there is one. */
static uint64_t first_fit(uint64_t from, uint64_t limit, uint64_t length, uint64_t alignment, int *found)
{
    uint64_t candidate = align_up(from, alignment);
    for (int i = 0; i < range_count; i++) {
        const uint64_t range_end = ranges[i].start + ranges[i].length;
        if (range_end <= candidate) continue;             /* wholly below the candidate */
        if (candidate + length <= ranges[i].start) break; /* the gap before this range is enough */
        candidate = align_up(range_end, alignment);       /* overlaps: try just past it */
    }
    *found = candidate + length <= limit;
    return candidate;
}

static int32_t allocate_direct_memory(int64_t search_start, int64_t search_end, uint64_t length, uint64_t alignment,
                                      int32_t memory_type, int64_t *start)
{
    static const char name[] = "sceKernelAllocateDirectMemory";
    if (!start) rt_fatal(name, "no variable for the start: the error value is not established");
    if (search_start < 0 || search_end <= search_start || (uint64_t)search_end > RT_DIRECT_MEMORY_SIZE)
        rt_fatal(name, "a search range outside the pool: the error value is not established");
    if (!length || !is_page_multiple(length))
        rt_fatal(name, "a length that is not a whole number of 16 KiB pages: the answer is not established");
    if (!is_power_of_two(alignment) || !is_page_multiple(alignment))
        rt_fatal(name, "an alignment that is not a power-of-two multiple of 16 KiB: the answer is not established");
    if (memory_type != MEMORY_TYPE_WB_ONION && memory_type != MEMORY_TYPE_WC_GARLIC &&
        memory_type != MEMORY_TYPE_WB_GARLIC)
        rt_fatal(name, "a memory type that is not established");

    pthread_mutex_lock(&pool_lock);
    pool(name);
    if (range_count == MAX_RANGES) rt_fatal(name, "more direct memory ranges than this runtime tracks");
    int found = 0;
    const uint64_t at = first_fit((uint64_t)search_start, (uint64_t)search_end, length, alignment, &found);
    if (!found) rt_fatal(name, "no room in the search range: the error value is not established");
    int slot = range_count;
    while (slot > 0 && ranges[slot - 1].start > at) {
        ranges[slot] = ranges[slot - 1];
        slot--;
    }
    ranges[slot] = (direct_range){at, length, memory_type, 0};
    range_count++;
    pthread_mutex_unlock(&pool_lock);
    *start = (int64_t)at;
    return 0;
}

/* The allocated range that holds [start, start + length), or -1. */
static int range_holding(uint64_t start, uint64_t length)
{
    for (int i = 0; i < range_count; i++)
        if (start >= ranges[i].start && start + length <= ranges[i].start + ranges[i].length) return i;
    return -1;
}

static int32_t map_direct_memory(void **address, uint64_t length, int32_t protection, int32_t flags, int64_t start,
                                 uint64_t alignment)
{
    static const char name[] = "sceKernelMapDirectMemory";
    if (!address) rt_fatal(name, "no variable for the address: the error value is not established");
    if (flags) rt_fatal(name, "map flags (a fixed address and the others) are not established");
    if (*address) rt_fatal(name, "an address hint without flags: how the system uses it is not established");
    /* The runtime has no GPU yet: GPU access is accepted and the host mapping takes the CPU bits. */
    if (!protection || (protection & ~(PROT_CPU_READ | PROT_CPU_WRITE | PROT_GPU_READ | PROT_GPU_WRITE)))
        rt_fatal(name, "protection other than CPU and GPU read and write is not established");
    if (!length || !is_page_multiple(length) || start < 0 || !is_page_multiple((uint64_t)start))
        rt_fatal(name, "a range that is not whole 16 KiB pages: the answer is not established");
    if (!is_power_of_two(alignment) || !is_page_multiple(alignment))
        rt_fatal(name, "an alignment that is not a power-of-two multiple of 16 KiB: the answer is not established");

    pthread_mutex_lock(&pool_lock);
    const int range = range_holding((uint64_t)start, length);
    if (range < 0) rt_fatal(name, "a range that was not allocated: the error value is not established");
    /* Reserve enough to place an aligned address, then put the range's pages there. */
    const uint64_t reserve = length + alignment;
    unsigned char *base = mmap(NULL, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED) rt_fatal(name, "the host has no address space for the map");
    unsigned char *aligned = (unsigned char *)(((uintptr_t)base + alignment - 1) & ~(uintptr_t)(alignment - 1));
    const int host_protection = (protection & PROT_CPU_READ ? PROT_READ : 0) | (protection & PROT_CPU_WRITE ? PROT_WRITE : 0);
    if (mmap(aligned, length, host_protection, MAP_SHARED | MAP_FIXED, pool(name), (off_t)start) == MAP_FAILED)
        rt_fatal(name, "the host cannot map the range");
    if (aligned > base) munmap(base, (size_t)(aligned - base));
    if (aligned + length < base + reserve) munmap(aligned + length, (size_t)(base + reserve - (aligned + length)));
    ranges[range].maps++;
    pthread_mutex_unlock(&pool_lock);
    *address = aligned;
    return 0;
}

static int32_t release_direct_memory(int64_t start, uint64_t length)
{
    static const char name[] = "sceKernelReleaseDirectMemory";
    pthread_mutex_lock(&pool_lock);
    int range = -1;
    for (int i = 0; i < range_count; i++)
        if (ranges[i].start == (uint64_t)start && ranges[i].length == length) range = i;
    if (range < 0) rt_fatal(name, "not one whole allocated range: releasing part or none is not established");
    if (ranges[range].maps) rt_fatal(name, "a range that is still mapped: what happens to the map is not established");
    for (int i = range; i + 1 < range_count; i++) ranges[i] = ranges[i + 1];
    range_count--;
    pthread_mutex_unlock(&pool_lock);
    return 0;
}

/* Flexible memory: int32_t sceKernelMapFlexibleMemory(void **address, size_t length, int32_t
   protection, int32_t flags) (the OpenOrbis homebrew SDK's libkernel.h, corroborating the shape).
   The executable's only call (0x02481dbe) presets the address to 0, rounds the length up to 16 KiB,
   passes protection 3 or 0x33 and flags 0, and on success requires a non-null address.

   The runtime maps fresh zeroed host memory at an address it picks, with the CPU bits of the
   protection (GPU access is accepted, as for direct memory). The PS4 limits a title's flexible
   memory to a budget; the budget is not established here and is not enforced. What else is not
   established stops by name: an address hint, map flags, other protection bits, a length that is
   not whole 16 KiB pages, and a missing variable. */
static int32_t map_flexible_memory(void **address, uint64_t length, int32_t protection, int32_t flags)
{
    static const char name[] = "sceKernelMapFlexibleMemory";
    if (!address) rt_fatal(name, "no variable for the address: the error value is not established");
    if (flags) rt_fatal(name, "map flags are not established");
    if (*address) rt_fatal(name, "an address hint: how the system uses it is not established");
    if (!protection || (protection & ~(PROT_CPU_READ | PROT_CPU_WRITE | PROT_GPU_READ | PROT_GPU_WRITE)))
        rt_fatal(name, "protection other than CPU and GPU read and write is not established");
    if (!length || !is_page_multiple(length))
        rt_fatal(name, "a length that is not whole 16 KiB pages: the answer is not established");
    const int host_protection = (protection & PROT_CPU_READ ? PROT_READ : 0) | (protection & PROT_CPU_WRITE ? PROT_WRITE : 0);
    void *const mapped = mmap(NULL, length, host_protection, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mapped == MAP_FAILED) rt_fatal(name, "the host cannot map the memory");
    *address = mapped;
    return 0;
}

RT_SYSLIB("libkernel", sceKernelGetDirectMemorySize, get_direct_memory_size);
RT_SYSLIB("libkernel", sceKernelAllocateDirectMemory, allocate_direct_memory);
RT_SYSLIB("libkernel", sceKernelMapDirectMemory, map_direct_memory);
RT_SYSLIB("libkernel", sceKernelReleaseDirectMemory, release_direct_memory);
RT_SYSLIB("libkernel", sceKernelMapFlexibleMemory, map_flexible_memory);
