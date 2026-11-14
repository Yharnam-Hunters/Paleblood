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
   does. Where a map goes is the system's choice when the title gives no address hint: a GPU-visible
   one (protection with GPU bits) goes at rt_gpu_memory_base() + its offset in the pool, so that the
   renderer's GPU reaches every such address at one fixed distance (runtime/gpu_memory.h); the
   others go anywhere free in the map window, which starts past that region. A second GPU-visible
   map of a range, which would need the same place, stops by name. Its size is this runtime's choice (RT_DIRECT_MEMORY_SIZE), not a measured PS4 value. Every
   case whose PS4 answer is not established stops by the function's name instead of guessing: map
   flags, an address hint in use or off the alignment, protection beyond CPU and GPU read/write, a range that does not fit, releasing
   part of an allocation or a mapped one, and the error values. */
#define _GNU_SOURCE
#include "runtime/gpu_memory.h"
#include "runtime/memory.h"
#include "runtime/guest_errno.h"
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/write_watch.h"

#include <errno.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <fcntl.h>
#include <stdio.h>
#endif
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The pool this runtime provides, and what sceKernelGetDirectMemorySize answers: a choice of the
   runtime, not the PS4's value (which is not established here). It is large, and sparse on the host
   (a memory file only takes host memory for the pages written). */
#define RT_DIRECT_MEMORY_SIZE (5ull << 30)
/* The granularity this runtime accepts for lengths, starts and alignments: 16 KiB, which every
   call the executable makes respects, and the page size getpagesize answers (runtime/syslib/kernel.c).
   Whether the PS4 accepts finer values is not established, so anything finer stops by name instead
   of being rounded. */
#define DIRECT_PAGE 0x4000ull
#define SCE_KERNEL_ERROR_EINVAL ((int32_t)0x80020016) /* 0x80020000 plus FreeBSD's EINVAL, 22 */

enum { PROT_CPU_READ = 0x1, PROT_CPU_WRITE = 0x2, PROT_GPU_READ = 0x10, PROT_GPU_WRITE = 0x20 };
enum { MAP_NO_OVERWRITE = 0x80 }; /* sceKernelMapDirectMemory flags (shadPS4 kernel memory.h) */
enum { MEMORY_TYPE_WB_ONION = 0x0, MEMORY_TYPE_WC_GARLIC = 0x3, MEMORY_TYPE_WB_GARLIC = 0xa };
enum { MAX_RANGES = 1024 };

typedef struct {
    uint64_t start, length;
    int32_t memory_type;
    int maps; /* how many host mappings of the range exist */
} direct_range;

static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static int pool_fd = -1;
#if defined(__APPLE__) && defined(__MACH__)
static FILE *pool_file; /* Retained for the process lifetime, like the Linux memfd. */
#endif
static direct_range ranges[MAX_RANGES]; /* allocated ranges, ascending by start */
static int range_count;


static int is_page_multiple(uint64_t value) { return value % DIRECT_PAGE == 0; }
static int is_power_of_two(uint64_t value) { return value && !(value & (value - 1)); }

/* The pool's file, created the first time a range is allocated. */
static int pool(const char *caller)
{
    if (pool_fd >= 0) return pool_fd;
#if defined(__APPLE__) && defined(__MACH__)
    FILE *file = tmpfile();
    if (!file) rt_fatal(caller, "cannot create the host file that holds direct memory");
    const int fd = fileno(file);
    const int descriptor_flags = fd < 0 ? -1 : fcntl(fd, F_GETFD);
    if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) ||
        ftruncate(fd, (off_t)RT_DIRECT_MEMORY_SIZE)) {
        (void)fclose(file);
        rt_fatal(caller, "cannot initialize the host file that holds direct memory");
    }
    pool_file = file; /* Publish ownership only after all preparation succeeds. */
#else
    const int fd = memfd_create("direct-memory", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)RT_DIRECT_MEMORY_SIZE) != 0)
        rt_fatal(caller, "cannot create the host file that holds direct memory");
#endif
    pool_fd = fd;
    return pool_fd;
}

static size_t get_direct_memory_size(void) { return (size_t)RT_DIRECT_MEMORY_SIZE; }

static uint64_t align_up(uint64_t value, uint64_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

/* Where a map goes when the title lets the system pick the address. The GPU reaches memory through
   40-bit addresses: AMD's public register formats keep a buffer's base as a 32-bit field holding
   the address shifted right by 8, and the engine stores addresses of memory it mapped that way (a
   render target's buffers, 0x01472830 read by 0x0257a210). A host address above 2^40, where Linux
   puts maps of its own choosing, loses its top bits there, so the runtime places every map it
   picks an address for inside this window below 2^40, clear of the executable and modules (from
   0x800000000), the thread objects (0xf000000000) and the tessellation ring (0xfe00000000). The
   window is the runtime's choice, not the PS4's layout. Addresses are handed out upward and not
   reused; running out of the window stops by name. */
#define MAP_WINDOW_START 0x2000000000ull
#define MAP_WINDOW_END 0xe000000000ull
/* The window's first RT_DIRECT_MEMORY_SIZE bytes hold GPU-visible direct maps at their pool
   offsets; everything else the system places goes after them. Darwin may already occupy the
   Linux window's start, so select a free aligned span without replacing any host allocation. */
static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;
#if defined(__APPLE__) && defined(__MACH__)
static uint64_t gpu_memory_base;
static uint64_t window_next;

/* window_lock held. This temporary probe selects a placement policy, not lifetime ownership:
   every later mapping must still claim its own range without replacing a concurrent mapping.
   Keep the original 128 GiB base alignment and the renderer's 40-bit address limit. */
static void initialize_window(const char *caller)
{
    if (gpu_memory_base) return;
    for (uint64_t base = MAP_WINDOW_START; base < MAP_WINDOW_END; base += MAP_WINDOW_START) {
        void *const probe = rt_map_at((void *)(uintptr_t)base, RT_DIRECT_MEMORY_SIZE, PROT_NONE,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (probe == MAP_FAILED) {
            if (errno == EEXIST || errno == EADDRNOTAVAIL) continue;
            rt_fatal(caller, "the host cannot probe the direct-memory address window");
        }
        if (munmap(probe, RT_DIRECT_MEMORY_SIZE))
            rt_fatal(caller, "the host cannot release the direct-memory address probe");
        gpu_memory_base = base;
        window_next = base + RT_DIRECT_MEMORY_SIZE;
        return;
    }
    rt_fatal(caller, "no aligned direct-memory address span in the runtime's map window");
}
#else
static uint64_t gpu_memory_base = MAP_WINDOW_START;
static uint64_t window_next = MAP_WINDOW_START + RT_DIRECT_MEMORY_SIZE;
static void initialize_window(const char *caller) { (void)caller; }
#endif

uint64_t rt_gpu_memory_base(void)
{
    pthread_mutex_lock(&window_lock);
    initialize_window("rt_gpu_memory_base");
    const uint64_t base = gpu_memory_base;
    pthread_mutex_unlock(&window_lock);
    return base;
}

int rt_gpu_memory_pool(int *fd, uint64_t *size)
{
    pthread_mutex_lock(&pool_lock);
    const int created = pool_fd >= 0;
    if (created) *fd = pool_fd, *size = RT_DIRECT_MEMORY_SIZE;
    pthread_mutex_unlock(&pool_lock);
    return created ? 0 : -1;
}

uint8_t *rt_gpu_memory_gds(void)
{
    static _Alignas(RT_GPU_GDS_BYTES) uint8_t gds[RT_GPU_GDS_BYTES];
    return gds;
}

/* Non-replacing placement in the address window. A declined POSIX hint is stepped over
   without assuming the reason was occupancy; other host failures still stop by name. */
static void *window_place(const char *caller, uint64_t length, uint64_t alignment, int host_protection, int map_flags,
                          int fd, off_t offset)
{
    pthread_mutex_lock(&window_lock);
    initialize_window(caller);
    for (;;) {
        const uint64_t at = align_up(window_next, alignment);
        if (at >= MAP_WINDOW_END || MAP_WINDOW_END - at < length) rt_fatal(caller, "the runtime's map window is full");
        void *const got = rt_map_at((void *)(uintptr_t)at, length, host_protection, map_flags, fd, offset);
        if (got == (void *)(uintptr_t)at) {
            window_next = at + length;
            pthread_mutex_unlock(&window_lock);
            return got;
        }
        if (errno != EEXIST && errno != EADDRNOTAVAIL) rt_fatal(caller, "the host cannot map the memory");
        window_next = at + alignment;
    }
}

/* The lowest aligned start at or above `from` where `length` bytes fit below `limit` without
   touching an allocated range (ranges are kept ascending); *found says whether there is one. */
static uint64_t first_fit(uint64_t from, uint64_t limit, uint64_t length, uint64_t alignment, int *found)
{
    uint64_t candidate = align_up(from, alignment);
    for (int i = 0; i < range_count; i++) {
        const uint64_t range_end = ranges[i].start + ranges[i].length;
        if (range_end <= candidate) continue;             /* wholly below the candidate */
        if (candidate <= ranges[i].start && length <= ranges[i].start - candidate) break; /* the gap before this range is enough */
        candidate = align_up(range_end, alignment);       /* overlaps: try just past it */
    }
    *found = candidate <= limit && length <= limit - candidate;
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
        if (start >= ranges[i].start && start - ranges[i].start < ranges[i].length
            && length <= ranges[i].length - (start - ranges[i].start)) return i;
    return -1;
}

/* The guest mappings the runtime made (direct and flexible memory), with the protection the guest
   asked for, GPU bits included, for sceKernelQueryMemoryProtection and sceKernelVirtualQuery; a
   direct map also keeps its offset in the pool and its range's memory type. sceKernelMunmap
   removes one. */
typedef struct {
    uintptr_t start;
    uint64_t length;
    int32_t protection;
    int direct;          /* 1: direct memory, 0: flexible memory */
    int64_t offset;      /* direct memory: where the map starts in the pool */
    int32_t memory_type; /* direct memory: its range's type */
} guest_mapping;

enum { MAX_MAPPINGS = 4096 };
static pthread_mutex_t mappings_lock = PTHREAD_MUTEX_INITIALIZER;
static guest_mapping mappings[MAX_MAPPINGS];
static int mapping_count;

/* Records a mapping. The CPU's writes to a direct map are watched (runtime/write_watch.h), so the
   renderer loads a texture again only when its memory was written; a watch that cannot start
   leaves the renderer taking all memory as changed. */
static void note_mapping(const char *caller, guest_mapping mapping)
{
    pthread_mutex_lock(&mappings_lock);
    if (mapping_count == MAX_MAPPINGS) rt_fatal(caller, "more mappings than the runtime records");
    mappings[mapping_count++] = mapping;
    const int host_protection = (mapping.protection & PROT_CPU_READ ? PROT_READ : 0) | (mapping.protection & PROT_CPU_WRITE ? PROT_WRITE : 0);
    if (mapping.direct) rt_write_watch_add((void *)mapping.start, mapping.length, (uint64_t)mapping.offset, host_protection);
    pthread_mutex_unlock(&mappings_lock);
}

/* The recorded mapping that holds an address, copied into *out; 0 when there is none. */
static int mapping_holding(uintptr_t at, guest_mapping *out)
{
    pthread_mutex_lock(&mappings_lock);
    int found = 0;
    for (int i = 0; i < mapping_count && !found; i++)
        if (at >= mappings[i].start && at - mappings[i].start < mappings[i].length) {
            *out = mappings[i];
            found = 1;
        }
    pthread_mutex_unlock(&mappings_lock);
    return found;
}

static int32_t map_direct_memory(void **address, uint64_t length, int32_t protection, int32_t flags, int64_t start,
                                 uint64_t alignment)
{
    static const char name[] = "sceKernelMapDirectMemory";
    if (!address) rt_fatal(name, "no variable for the address: the error value is not established");
    /* No overwrite (0x80, shadPS4's kernel memory.h) keeps a map off memory already mapped; the runtime
       never replaces a mapping (hints use non-replacing placement), so it changes nothing. */
    if (flags & ~MAP_NO_OVERWRITE) rt_fatal(name, "map flags other than no-overwrite (a fixed address and the others) are not established");
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
    const int host_protection = (protection & PROT_CPU_READ ? PROT_READ : 0) | (protection & PROT_CPU_WRITE ? PROT_WRITE : 0);
    unsigned char *aligned;
    if (*address) {
        /* An address hint, as FreeBSD's mmap takes one without MAP_FIXED: the map goes there when the
           range is free. A hint in use (where the system then maps is not established) and one off
           the alignment stop by name. */
        aligned = *address;
        if ((uintptr_t)aligned & (alignment - 1)) rt_fatal(name, "an address hint off the alignment: the answer is not established");
        void *const got = rt_map_at(aligned, length, host_protection, MAP_SHARED, pool(name), (off_t)start);
        if (got != aligned) rt_fatal(name, "an address hint in use: where the system then maps is not established");
    } else if (protection & (PROT_GPU_READ | PROT_GPU_WRITE)) {
        /* At its offset in the pool, where the GPU reaches it (runtime/gpu_memory.h). The base is
           aligned far beyond any map alignment, so the address is aligned when the offset is. */
        aligned = (unsigned char *)(uintptr_t)(rt_gpu_memory_base() + (uint64_t)start);
        if ((uint64_t)start & (alignment - 1))
            rt_fatal(name, "a GPU-visible map whose pool offset is off its alignment: where it goes is not established");
        void *const got = rt_map_at(aligned, length, host_protection, MAP_SHARED, pool(name), (off_t)start);
        if (got != aligned)
            rt_fatal(name, "a second GPU-visible map of a pool range: the runtime keeps one, at the range's GPU address");
    } else {
        aligned = window_place(name, length, alignment, host_protection, MAP_SHARED, pool(name), (off_t)start);
    }
    ranges[range].maps++;
    const int32_t memory_type = ranges[range].memory_type;
    pthread_mutex_unlock(&pool_lock);
    note_mapping(name, (guest_mapping){(uintptr_t)aligned, length, protection, 1, start, memory_type});
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

   The runtime maps fresh zeroed host memory at an address it picks in its map window, with the CPU bits of the
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
    void *const mapped = window_place(name, length, DIRECT_PAGE, host_protection,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    note_mapping(name, (guest_mapping){(uintptr_t)mapped, length, protection, 0, 0, 0});
    *address = mapped;
    return 0;
}

/* sceKernelQueryMemoryProtection(void *address, void **start, void **end, int32_t *protection), as
   the OpenOrbis homebrew SDK's libkernel.h declares it: the protection of the mapping that holds
   an address. The executable's helpers (0x02abad98, 0x02abadf8) pass no start or end and test the
   protection's CPU read bit (1) and GPU read bit (0x10) of memory it mapped; another
   (0x02d031df, reached once the movie player runs) passes a start and an end and reads only the
   protection. The runtime answers the protection asked for when it made the mapping, and the
   mapping's start and its end one past the last byte (exclusive), as shadPS4's
   MemoryManager::QueryProtection and PS4Recomp's ps4_direct_mem.c both answer. An address outside
   the mappings it made (other memory is not tracked yet) and a missing protection variable stop by
   name. */
static int32_t query_memory_protection(const void *address, void **start, void **end, int32_t *protection)
{
    static const char name[] = "sceKernelQueryMemoryProtection";
    if (!protection) rt_fatal(name, "no variable for the protection: the error value is not established");
    guest_mapping m;
    if (!mapping_holding((uintptr_t)address, &m))
        rt_fatal(name, "an address outside the runtime's direct and flexible mappings: not tracked yet");
    *protection = m.protection;
    if (start) *start = (void *)m.start;
    if (end) *end = (void *)(m.start + m.length); /* exclusive */
    return 0;
}

/* sceKernelVirtualQuery(const void *address, int32_t flags, info *, size_t size), as the OpenOrbis
   homebrew SDK's libkernel.h and _types/kernel.h (OrbisKernelVirtualQueryInfo) declare it: the
   mapping that holds an address, in a 0x48-byte structure: start and end (0x00, 0x08), the direct
   memory offset (0x10), protection (0x18), memory type (0x1c), flag bits at 0x20 (flexible 1,
   direct 2, stack 4, pooled 8, committed 0x10) and a 32-byte name (0x24). Every call in the
   executable passes flags 0 and size 0x48, and reads the start, the offset or the direct bit: one
   (0x02d0328c) works out an address's place in direct memory as offset + (address - start), others
   release the direct memory at the offset (0x02ad3f5e, 0x02ad4066, 0x02ad423e).

   The runtime answers from its record of direct and flexible mappings: start, offset, protection
   as asked for, memory type, flexible or direct, committed, no name. No call reads the end, and
   whether it is inclusive is not established; the runtime writes the address just past the
   mapping. Flags other than 0, another size, an address outside those mappings (other memory is
   not tracked yet) and a missing structure stop by name. */
enum { VIRTUAL_QUERY_INFO_SIZE = 0x48, VQ_FLEXIBLE = 0x1, VQ_DIRECT = 0x2, VQ_COMMITTED = 0x10 };

typedef struct {
    uint64_t start, end;
    int64_t offset;
    int32_t protection, memory_type;
    uint32_t bits;
    char name[32];
} virtual_query_info;
_Static_assert(sizeof(virtual_query_info) == VIRTUAL_QUERY_INFO_SIZE, "OrbisKernelVirtualQueryInfo is 0x48 bytes");

static int32_t virtual_query(const void *address, int32_t flags, virtual_query_info *info, uint64_t size)
{
    static const char name[] = "sceKernelVirtualQuery";
    if (flags) rt_fatal(name, "flags other than 0 are not established");
    if (!info || size != VIRTUAL_QUERY_INFO_SIZE) rt_fatal(name, "no structure, or a size other than 0x48");
    guest_mapping m;
    if (!mapping_holding((uintptr_t)address, &m))
        rt_fatal(name, "an address outside the runtime's direct and flexible mappings: not tracked yet");
    *info = (virtual_query_info){
        .start = m.start,
        .end = m.start + m.length,
        .offset = m.direct ? m.offset : 0,
        .protection = m.protection,
        .memory_type = m.direct ? m.memory_type : 0,
        .bits = (m.direct ? VQ_DIRECT : VQ_FLEXIBLE) | VQ_COMMITTED,
    };
    return 0;
}

/* sceKernelMunmap(void *address, size_t length), sceKernelMprotect(const void *address, size_t
   length, int protection) and sceKernelGetDirectMemoryType(off_t start, int32_t *type, off_t
   *range_start, off_t *range_end), as OpenOrbis' libkernel.h declares them.

   Munmap is FreeBSD's munmap(2) under its SCE name. The executable frees a direct map with
   munmap and then sceKernelReleaseDirectMemory (0x024826fd, 0x0248270c), the order the release
   requires. A zero length is EINVAL (FreeBSD's sys_munmap; shadPS4's sceKernelMunmap answers the
   same). As in FreeBSD, the range may span several mappings, reserved space and holes: every
   mapping the runtime made inside it goes (a direct map no longer holds its range, which can then
   be released), reserved space inside it is given back, and a hole is left as it is. The
   executable relies on that: its block allocator (0x02d01eb0) unmaps a reserved range whose
   pieces it mapped and unmapped one by one (sceKernelBatchMap, below). Addresses are not handed
   out again.

   Mprotect is FreeBSD's mprotect(2): the mapping takes the new protection, CPU bits on the host,
   GPU bits recorded, as for a map. The executable's one call (0x02d03214) is a wrapper that answers
   whether the result was 0.

   GetDirectMemoryType answers the memory type of the allocated range that holds a pool offset,
   with the range's start and end (just past it, as for sceKernelVirtualQuery). The executable's
   one call (0x02abae74) passes the offset sceKernelVirtualQuery gave for an address and only tests
   whether the type is 3 (write-combined garlic).

   What stops by name: part of a mapping; for munmap, a start off the 16 KiB page, and a range that
   touches nothing the runtime mapped or reserved outside its map window (other memory is not
   tracked); for mprotect, more than one mapping or memory the runtime did not map; an offset
   outside every allocated range (shadPS4 answers ENOENT; no public source settles it); a
   protection beyond CPU and GPU read and write; missing variables. */
static int mapping_index(const char *caller, uintptr_t start, uint64_t length)
{
    for (int i = 0; i < mapping_count; i++) {
        if (start < mappings[i].start || start - mappings[i].start >= mappings[i].length) continue;
        if (start != mappings[i].start || length != mappings[i].length)
            rt_fatal(caller, "part of a mapping, or more than one: not modelled");
        return i;
    }
    rt_fatal(caller, "memory the runtime did not map: not tracked yet");
}

/* Address space reserved by sceKernelReserveVirtualRange and not mapped since: host PROT_NONE
   memory the guest maps into with sceKernelBatchMap. Guarded by mappings_lock. */
typedef struct {
    uintptr_t start;
    uint64_t length;
} reserved_space;

enum { MAX_RESERVATIONS = 1024 };
static reserved_space reservations[MAX_RESERVATIONS];
static int reservation_count;

static int overlaps(uintptr_t start, uint64_t length, uintptr_t other, uint64_t other_length)
{
    return start < other + other_length && other < start + length;
}

static void note_reservation(const char *caller, uintptr_t start, uint64_t length)
{
    if (!length) return;
    if (reservation_count == MAX_RESERVATIONS) rt_fatal(caller, "more reserved ranges than the runtime records");
    reservations[reservation_count++] = (reserved_space){start, length};
}

/* Takes [start, start + length) out of the reserved space (mappings_lock held): what overlaps it is
   cut away, keeping what lies on either side. */
static void unreserve(const char *caller, uintptr_t start, uint64_t length)
{
    for (int i = 0; i < reservation_count;) {
        const reserved_space r = reservations[i];
        if (!overlaps(start, length, r.start, r.length)) {
            i++;
            continue;
        }
        reservations[i] = reservations[--reservation_count];
        if (r.start < start) note_reservation(caller, r.start, start - r.start);
        if (r.start + r.length > start + length) note_reservation(caller, start + length, r.start + r.length - (start + length));
    }
}

/* Whether one reserved piece holds all of [start, start + length) (mappings_lock held). */
static int is_reserved(uintptr_t start, uint64_t length)
{
    for (int i = 0; i < reservation_count; i++) {
        const reserved_space r = reservations[i];
        if (start >= r.start && start - r.start < r.length && length <= r.length - (start - r.start)) return 1;
    }
    return 0;
}

static int32_t unmap_range(const char *caller, void *address, uint64_t length, int flexible_only)
{
    if (!length) return SCE_KERNEL_ERROR_EINVAL;
    const uintptr_t start = (uintptr_t)address;
    if (start % DIRECT_PAGE) rt_fatal(caller, "a start off the 16 KiB page: the answer is not established");
    if (start + length < start) rt_fatal(caller, "a range that wraps around the address space");
    pthread_mutex_lock(&mappings_lock);
    int touched = 0;
    for (int i = 0; i < mapping_count; i++)
        if (overlaps(start, length, mappings[i].start, mappings[i].length)) {
            if (mappings[i].start < start || mappings[i].start + mappings[i].length > start + length)
                rt_fatal(caller, "part of a mapping: not modelled");
            if (flexible_only && mappings[i].direct) rt_fatal(caller, "a direct memory map: not flexible memory");
            touched = 1;
        }
    for (int i = 0; i < reservation_count; i++) touched |= overlaps(start, length, reservations[i].start, reservations[i].length);
    const int in_window = start >= MAP_WINDOW_START && start + length <= MAP_WINDOW_END;
    if (!touched && !in_window) rt_fatal(caller, "memory the runtime did not map: not tracked yet");

    for (int i = 0; i < mapping_count;) {
        const guest_mapping m = mappings[i];
        if (!overlaps(start, length, m.start, m.length)) {
            i++;
            continue;
        }
        if (m.direct) rt_write_watch_remove((void *)m.start, m.length);
        if (munmap((void *)m.start, m.length)) rt_fatal(caller, "the host could not unmap the memory");
        mappings[i] = mappings[--mapping_count];
        if (m.direct) {
            pthread_mutex_lock(&pool_lock);
            const int range = range_holding((uint64_t)m.offset, m.length);
            if (range >= 0) ranges[range].maps--;
            pthread_mutex_unlock(&pool_lock);
        }
    }
    for (int i = 0; i < reservation_count; i++) {
        const reserved_space r = reservations[i];
        if (!overlaps(start, length, r.start, r.length)) continue;
        const uintptr_t from = r.start > start ? r.start : start;
        const uintptr_t to = r.start + r.length < start + length ? r.start + r.length : start + length;
        if (munmap((void *)from, to - from)) rt_fatal(caller, "the host could not give back the reserved space");
    }
    unreserve(caller, start, length);
    pthread_mutex_unlock(&mappings_lock);
    return 0;
}

static int32_t kernel_munmap(void *address, uint64_t length) { return unmap_range("sceKernelMunmap", address, length, 0); }

static int32_t kernel_mprotect(const void *address, uint64_t length, int32_t protection)
{
    static const char name[] = "sceKernelMprotect";
    if (protection & ~(PROT_CPU_READ | PROT_CPU_WRITE | PROT_GPU_READ | PROT_GPU_WRITE))
        rt_fatal(name, "protection other than CPU and GPU read and write is not established");
    pthread_mutex_lock(&mappings_lock);
    const int i = mapping_index(name, (uintptr_t)address, length);
    const int host_protection = (protection & PROT_CPU_READ ? PROT_READ : 0) | (protection & PROT_CPU_WRITE ? PROT_WRITE : 0);
    /* memory the renderer guards (runtime/write_watch.h) is made current first, then keeps the new protection */
    rt_write_watch_touch(address, length);
    if (mprotect((void *)(uintptr_t)address, length, host_protection)) rt_fatal(name, "the host refused the protection");
    rt_write_watch_protect((void *)(uintptr_t)address, length, host_protection);
    mappings[i].protection = protection;
    pthread_mutex_unlock(&mappings_lock);
    return 0;
}

static int32_t get_direct_memory_type(int64_t start, int32_t *type, int64_t *range_start, int64_t *range_end)
{
    static const char name[] = "sceKernelGetDirectMemoryType";
    if (!type || !range_start || !range_end) rt_fatal(name, "no variable for an answer: the error value is not established");
    pthread_mutex_lock(&pool_lock);
    const int range = start < 0 ? -1 : range_holding((uint64_t)start, 1);
    if (range < 0) rt_fatal(name, "an offset outside every allocated range: the error value is not established");
    *type = ranges[range].memory_type;
    *range_start = (int64_t)ranges[range].start;
    *range_end = (int64_t)(ranges[range].start + ranges[range].length);
    pthread_mutex_unlock(&pool_lock);
    return 0;
}

/* The POSIX memory calls libc.elf imports, and two more SCE ones its heap uses.

   mmap(address, length, protection, flags, fd, offset) and munmap(address, length): POSIX/FreeBSD
   mmap(2) and munmap(2), answering MAP_FAILED (-1) or -1 with errno. libc.elf maps anonymous
   private memory, (NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0), for
   libunwind and its allocators (+0x52fac, +0x54f25, +0x5536d, +0x578c5, +0x57912) and unmaps
   exactly what it mapped. As in shadPS4's posix_mmap and a family of native ports (facts only), an
   anonymous map is flexible memory: the length rounded up to 16 KiB, placed by
   sceKernelMapFlexibleMemory, and munmap is sceKernelMunmap with its error number in errno.
   FreeBSD's rules give EINVAL for a zero length and for MAP_ANON with a descriptor other than -1 or
   an offset other than 0. What stops by name: a file mapping (+0x530e1, after sysctl), an address
   hint, other flags and protections.

   msync(address, length, flags): FreeBSD msync(2). libunwind uses it as a probe of whether memory
   is mapped (+0x541a5: MS_ASYNC on one or two 4 KiB pages; -1 means not readable), so a constant 0
   would be wrong (it would make libunwind read unmapped memory). The runtime asks the host,
   rounding to the host's page, and answers its result: 0, or -1 with ENOMEM (12) for memory not
   mapped. Other host errors stop by name.

   sceKernelMapNamedFlexibleMemory(address, length, protection, flags, name): libc.elf's heap
   (+0x23ec0, +0x23ff9, +0x24932) maps flexible memory with a debug name; the runtime maps it as
   sceKernelMapFlexibleMemory does. shadPS4's checks (facts only): no name is 0x8002000e (EFAULT),
   a name of 32 characters or more 0x8002003f (ENAMETOOLONG).

   sceKernelMlock(address, length): libc.elf calls it (+0x23ed9) only when a heap flag asks for
   locked memory. Host memory is not paged out the way the PS4 would page it, so locking changes
   nothing: the answer is 0, or EINVAL for no address, a zero length or a range that wraps (shadPS4
   and PS4Recomp also answer 0). */
enum { POSIX_PROT_RW = PROT_CPU_READ | PROT_CPU_WRITE, POSIX_MAP_PRIVATE = 0x2, POSIX_MAP_ANON = 0x1000 };
enum { BSD_ENOMEM = 12, BSD_EINVAL_NUMBER = 22, NAMED_MEMORY_NAME_MAX = 31 };
#define POSIX_MAP_FAILED ((void *)-1)
#define SCE_KERNEL_ERROR_EFAULT ((int32_t)0x8002000e)
#define SCE_KERNEL_ERROR_ENAMETOOLONG ((int32_t)0x8002003f)

static void *posix_mmap(void *address, uint64_t length, int32_t protection, int32_t flags, int32_t fd, int64_t offset)
{
    static const char fn[] = "mmap";
    if (!(flags & POSIX_MAP_ANON)) rt_fatal(fn, "a file mapping: not modelled");
    if (!length || fd != -1 || offset != 0) {
        *rt_guest_errno() = BSD_EINVAL_NUMBER;
        return POSIX_MAP_FAILED;
    }
    if (address || flags != (POSIX_MAP_ANON | POSIX_MAP_PRIVATE) || protection != POSIX_PROT_RW)
        rt_fatal(fn, "an address hint, other flags or other protection: not modelled");
    void *mapped = NULL;
    map_flexible_memory(&mapped, align_up(length, DIRECT_PAGE), protection, 0);
    return mapped;
}

static int32_t posix_munmap(void *address, uint64_t length)
{
    const int32_t answer = kernel_munmap(address, length);
    if (!answer) return 0;
    *rt_guest_errno() = (int32_t)((uint32_t)answer - 0x80020000u);
    return -1;
}

static int32_t posix_msync(void *address, uint64_t length, int32_t flags)
{
    (void)flags;
    const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    const uintptr_t start = (uintptr_t)address & ~(page - 1), end = (uintptr_t)address + length;
    if (!msync((void *)start, end - start, MS_ASYNC)) return 0;
    if (errno != ENOMEM) rt_fatal("msync", "a host error without an established answer");
    *rt_guest_errno() = BSD_ENOMEM;
    return -1;
}

static int32_t map_named_flexible_memory(void **address, uint64_t length, int32_t protection, int32_t flags, const char *name)
{
    if (!name) return SCE_KERNEL_ERROR_EFAULT;
    if (strnlen(name, NAMED_MEMORY_NAME_MAX + 1) > NAMED_MEMORY_NAME_MAX) return SCE_KERNEL_ERROR_ENAMETOOLONG;
    return map_flexible_memory(address, length, protection, flags);
}

static int32_t kernel_mlock(const void *address, uint64_t length)
{
    if (!address || !length || (uintptr_t)address + length < (uintptr_t)address) return SCE_KERNEL_ERROR_EINVAL;
    return 0;
}

/* Reserving address space and mapping into it.

   sceKernelReserveVirtualRange(void **address, size_t length, int flags, size_t alignment): address
   space no memory backs yet (shadPS4 maps it with no access; facts only), for maps the title places
   itself. The executable's block allocator (0x02d01eb0) reserves room for all its blocks at once
   with flags 0x80 (no overwrite) and alignment 0, then maps an alias of each block into it with
   sceKernelBatchMap. shadPS4's checks, which a family of native ports share (facts only), answer
   EINVAL (0x80020016) for no variable, a length that is 0 or not whole 16 KiB pages, and an
   alignment that is not 0 or a power-of-two multiple of 16 KiB up to 2 GiB. The runtime reserves
   inaccessible host memory in its map window. An address hint and flags other than no overwrite
   stop by name.

   sceKernelBatchMap(entries, count, int *processed): a list of 32-byte map operations (shadPS4's
   memory.h: start 0x00, offset 0x08, length 0x10, protection u8 0x18, type u8 0x19, operation s32
   0x1c), carried out in order until one fails; *processed (when given) says how many succeeded.
   shadPS4, a family of native ports and droogie's host agree that each map is placed at its
   start exactly (an implicit MAP_FIXED) - the point of the call. No list, a zero length or an
   operation above 4 answers EINVAL. The executable uses two operations: map direct memory (0) at
   the start, an alias of memory it already mapped (its offset found with sceKernelVirtualQuery,
   protection from the allocator), and unmap (1), which it applies to every entry when it frees the
   blocks or when a map failed, before unmapping the whole reserved range. The runtime maps the
   pool's range over the reserved space with the CPU bits; protect (2) is sceKernelMprotect. What
   stops by name: a map outside reserved space (what a fixed map replaces is not modelled), an
   aliased map with GPU access (the GPU reaches direct memory at one address per pool offset,
   runtime/gpu_memory.h), and the flexible-memory and type-protect operations (3, 4), which the
   executable does not use.

   sceKernelReleaseFlexibleMemory(void *address, size_t length): the executable's allocator frees a
   whole flexible block with it (0x02482721). The runtime unmaps it, as a family of native ports
   and droogie's host do (shadPS4 keeps the mapping and drops its pages; facts only), the
   maintainer's ruling of 2026-11-10. No address, a zero length or one that is not whole 16 KiB pages
   answer EINVAL; direct memory in the range stops by name. */
enum { RESERVE_ALIGNMENT_MAX = 0x80000000u };
enum { BATCH_MAP_DIRECT = 0, BATCH_UNMAP = 1, BATCH_PROTECT = 2, BATCH_MAP_FLEXIBLE = 3, BATCH_TYPE_PROTECT = 4 };

static int32_t reserve_virtual_range(void **address, uint64_t length, int32_t flags, uint64_t alignment)
{
    static const char name[] = "sceKernelReserveVirtualRange";
    if (!address || !length || !is_page_multiple(length)) return SCE_KERNEL_ERROR_EINVAL;
    if (alignment && (!is_power_of_two(alignment) || !is_page_multiple(alignment) || alignment > RESERVE_ALIGNMENT_MAX))
        return SCE_KERNEL_ERROR_EINVAL;
    if (flags & ~MAP_NO_OVERWRITE) rt_fatal(name, "flags other than no-overwrite (a fixed address and the others) are not established");
    if (*address) rt_fatal(name, "an address hint: how the system uses it is not established");
    void *const reserved = window_place(name, length, alignment ? alignment : DIRECT_PAGE, PROT_NONE,
                                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    pthread_mutex_lock(&mappings_lock);
    note_reservation(name, (uintptr_t)reserved, length);
    pthread_mutex_unlock(&mappings_lock);
    *address = reserved;
    return 0;
}

typedef struct {
    void *start;
    int64_t offset;
    uint64_t length;
    uint8_t protection, type;
    int16_t reserved;
    int32_t operation;
} batch_map_entry;
_Static_assert(sizeof(batch_map_entry) == 32, "a batch map entry is 32 bytes");

/* Maps a range of the pool at exactly `start`, inside reserved space (operation 0 of a batch). */
static void map_direct_fixed(const char *caller, const batch_map_entry *entry)
{
    const uintptr_t start = (uintptr_t)entry->start;
    if (start % DIRECT_PAGE || !is_page_multiple(entry->length) || entry->offset < 0 || !is_page_multiple((uint64_t)entry->offset))
        rt_fatal(caller, "a map that is not whole 16 KiB pages: the answer is not established");
    if (entry->protection & (PROT_GPU_READ | PROT_GPU_WRITE))
        rt_fatal(caller, "an aliased GPU-visible map: the GPU reaches each pool range at one address");
    if (!entry->protection || (entry->protection & ~(PROT_CPU_READ | PROT_CPU_WRITE)))
        rt_fatal(caller, "protection other than CPU read and write is not established");
    pthread_mutex_lock(&pool_lock);
    const int range = range_holding((uint64_t)entry->offset, entry->length);
    if (range < 0) rt_fatal(caller, "a range that was not allocated: the error value is not established");
    ranges[range].maps++;
    const int32_t memory_type = ranges[range].memory_type;
    pthread_mutex_unlock(&pool_lock);

    pthread_mutex_lock(&mappings_lock);
    if (!is_reserved(start, entry->length)) rt_fatal(caller, "a fixed map outside reserved space: what it replaces is not modelled");
    const int host_protection = (entry->protection & PROT_CPU_READ ? PROT_READ : 0) | (entry->protection & PROT_CPU_WRITE ? PROT_WRITE : 0);
    void *const got = mmap(entry->start, entry->length, host_protection, MAP_SHARED | MAP_FIXED, pool(caller), (off_t)entry->offset);
    if (got != entry->start) rt_fatal(caller, "the host could not map the memory at the address");
    unreserve(caller, start, entry->length);
    pthread_mutex_unlock(&mappings_lock);
    note_mapping(caller, (guest_mapping){start, entry->length, entry->protection, 1, entry->offset, memory_type});
}

static int32_t batch_map(batch_map_entry *entries, int32_t count, int32_t *processed)
{
    static const char name[] = "sceKernelBatchMap";
    int32_t answer = 0, done = 0;
    for (; done < count; done++) {
        batch_map_entry *const e = entries ? &entries[done] : NULL;
        if (!e || !e->length || e->operation < 0 || e->operation > BATCH_TYPE_PROTECT) {
            answer = SCE_KERNEL_ERROR_EINVAL;
            break;
        }
        switch (e->operation) {
        case BATCH_MAP_DIRECT: map_direct_fixed(name, e); break;
        case BATCH_UNMAP: answer = unmap_range(name, e->start, e->length, 0); break;
        case BATCH_PROTECT: answer = kernel_mprotect(e->start, e->length, e->protection); break;
        default: rt_fatal(name, "the flexible-memory map and type-protect operations are not modelled");
        }
        if (answer) break;
    }
    if (processed) *processed = done;
    return answer;
}

static int32_t release_flexible_memory(void *address, uint64_t length)
{
    if (!address || !length || !is_page_multiple(length)) return SCE_KERNEL_ERROR_EINVAL;
    return unmap_range("sceKernelReleaseFlexibleMemory", address, length, 1);
}

RT_SYSLIB("libkernel", sceKernelReserveVirtualRange, reserve_virtual_range);
RT_SYSLIB("libkernel", sceKernelBatchMap, batch_map);
RT_SYSLIB("libkernel", sceKernelReleaseFlexibleMemory, release_flexible_memory);
RT_SYSLIB("libkernel", sceKernelMunmap, kernel_munmap);
RT_SYSLIB("libkernel", sceKernelMprotect, kernel_mprotect);
RT_SYSLIB("libkernel", sceKernelGetDirectMemoryType, get_direct_memory_type);
RT_SYSLIB("libkernel", sceKernelGetDirectMemorySize, get_direct_memory_size);
RT_SYSLIB("libkernel", sceKernelAllocateDirectMemory, allocate_direct_memory);
RT_SYSLIB("libkernel", sceKernelMapDirectMemory, map_direct_memory);
RT_SYSLIB("libkernel", sceKernelReleaseDirectMemory, release_direct_memory);
RT_SYSLIB("libkernel", sceKernelMapFlexibleMemory, map_flexible_memory);
RT_SYSLIB("libkernel", sceKernelQueryMemoryProtection, query_memory_protection);
RT_SYSLIB("libkernel", sceKernelVirtualQuery, virtual_query);
RT_SYSLIB("libkernel", mmap, posix_mmap);
RT_SYSLIB("libkernel", munmap, posix_munmap);
RT_SYSLIB("libkernel", msync, posix_msync);
RT_SYSLIB("libkernel", sceKernelMapNamedFlexibleMemory, map_named_flexible_memory);
RT_SYSLIB("libkernel", sceKernelMlock, kernel_mlock);
