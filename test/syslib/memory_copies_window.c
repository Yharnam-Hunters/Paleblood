/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer's copies' guest addresses (rt_gpu_copies_window, runtime/syslib/kernel_memory.c): a span
   below 2^40 that no guest map has, and none where the host already holds memory there. Maps the system places (direct, flexible, reserved) land outside
   it, and a direct map at an address hint inside it stops by name, where the same map at a free
   address just below it is made. */
#define _GNU_SOURCE
#include "../syslib_test.h"

#include "runtime/gpu_memory.h"

#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef size_t (*f_size)(void);
typedef int32_t (*f_alloc)(int64_t, int64_t, uint64_t, uint64_t, int32_t, int64_t *);
typedef int32_t (*f_map_direct)(void **, uint64_t, int32_t, int32_t, int64_t, uint64_t);
typedef int32_t (*f_release)(int64_t, uint64_t);
typedef int32_t (*f_map_flexible)(void **, uint64_t, int32_t, int32_t);
typedef int32_t (*f_munmap)(void *, uint64_t);
typedef int32_t (*f_reserve)(void **, uint64_t, int32_t, uint64_t);

enum { PAGE = 0x4000, CPU_READ_WRITE = 3, GPU_READ_WRITE = 0x33, NO_OVERWRITE = 0x80 };

static f_map_direct map_direct;
static int64_t pool_offset;
static uint64_t window, window_bytes;

static void map_at(uint64_t hint)
{
    void *address = (void *)(uintptr_t)hint;
    map_direct(&address, PAGE, CPU_READ_WRITE, 0, pool_offset, PAGE);
}
static void map_at_window_start(void) { map_at(window); }
static void map_at_window_end(void) { map_at(window + window_bytes - PAGE); }

/* In a child process, which has not asked for the span yet: its first address (*start; 0 when the host
   declines it), then, in a second child, whether the runtime gives no span once the host holds a page
   at the span's end. The reservation replaces nothing, so it must fail there. */
static int declined_when_occupied(uint64_t *start)
{
    int channel[2], status = 0;
    uint64_t found[2] = {0, 0};
    if (pipe(channel)) return 0;
    pid_t child = fork();
    if (!child) {
        found[0] = rt_gpu_copies_window(&found[1]);
        _exit(write(channel[1], found, sizeof found) == (ssize_t)sizeof found ? 0 : 1);
    }
    const int told = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status) &&
                     read(channel[0], found, sizeof found) == (ssize_t)sizeof found;
    close(channel[0]), close(channel[1]);
    *start = told ? found[0] : 0;
    if (!told || !found[0]) return 0;
    child = fork();
    if (!child) {
        void *const last = (void *)(uintptr_t)(found[0] + found[1] - PAGE);
        uint64_t bytes = 1;
        if (mmap(last, PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != last) _exit(2);
        _exit(rt_gpu_copies_window(&bytes) == 0 && bytes == 0 ? 0 : 1);
    }
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status);
}

static int outside_window(const void *address, uint64_t length)
{
    const uint64_t start = (uintptr_t)address;
    return start + length <= window || start >= window + window_bytes;
}

SYSLIB_TEST(memory_copies_window)
{
    const f_size pool_size = (f_size)fn("libkernel", "sceKernelGetDirectMemorySize");
    const f_alloc allocate = (f_alloc)fn("libkernel", "sceKernelAllocateDirectMemory");
    const f_release release = (f_release)fn("libkernel", "sceKernelReleaseDirectMemory");
    const f_map_flexible map_flexible = (f_map_flexible)fn("libkernel", "sceKernelMapFlexibleMemory");
    const f_munmap unmap = (f_munmap)fn("libkernel", "sceKernelMunmap");
    const f_reserve reserve = (f_reserve)fn("libkernel", "sceKernelReserveVirtualRange");
    map_direct = (f_map_direct)fn("libkernel", "sceKernelMapDirectMemory");
    if (!pool_size || !allocate || !release || !map_flexible || !unmap || !reserve || !map_direct) return;

    uint64_t again = 1, expected = 0;
    const int declined = declined_when_occupied(&expected);
    window = rt_gpu_copies_window(&window_bytes);
    CHECK(window == expected && (declined || !window));
#if !(defined(__APPLE__) && defined(__MACH__))
    CHECK(window); /* nothing of the host's lies there on Linux */
#endif
    if (!window) {
        CHECK(!window_bytes); /* the host holds memory there: no span, and the renderer makes no copies */
        return;
    }
    CHECK(window_bytes >= 1ull << 30 && window % PAGE == 0 && window + window_bytes <= 1ull << 40);
    CHECK(rt_gpu_copies_window(&again) == window && again == window_bytes);
    int pool_fd = -1;
    uint64_t pool_bytes = 0;
    CHECK(allocate(0, (int64_t)pool_size(), PAGE, PAGE, 0, &pool_offset) == 0);
    CHECK(rt_gpu_memory_pool(&pool_fd, &pool_bytes) == 0 && outside_window((void *)(uintptr_t)rt_gpu_memory_base(), pool_bytes));

    /* what the system places: a GPU-visible direct map (in the pool's span), one for the CPU, flexible
       memory and reserved space (the map window) */
    void *gpu = NULL, *cpu = NULL, *flexible = NULL, *reserved = NULL;
    CHECK(map_direct(&gpu, PAGE, GPU_READ_WRITE, 0, pool_offset, PAGE) == 0 && gpu && outside_window(gpu, PAGE));
    CHECK(map_direct(&cpu, PAGE, CPU_READ_WRITE, 0, pool_offset, PAGE) == 0 && cpu && outside_window(cpu, PAGE));
    CHECK(map_flexible(&flexible, PAGE, CPU_READ_WRITE, 0) == 0 && flexible && outside_window(flexible, PAGE));
    CHECK(reserve(&reserved, PAGE, NO_OVERWRITE, 0) == 0 && reserved && outside_window(reserved, PAGE));

    /* an address hint: taken at a free address right below the span, a stop inside it */
    void *below = (void *)(uintptr_t)(window - PAGE);
    CHECK(map_direct(&below, PAGE, CPU_READ_WRITE, 0, pool_offset, PAGE) == 0 && (uintptr_t)below == window - PAGE);
    CHECK(died_in_fatal(map_at_window_start) && died_in_fatal(map_at_window_end));

    CHECK(unmap(below, PAGE) == 0 && unmap(reserved, PAGE) == 0 && unmap(flexible, PAGE) == 0);
    CHECK(unmap(cpu, PAGE) == 0 && unmap(gpu, PAGE) == 0 && release(pool_offset, PAGE) == 0);
}
