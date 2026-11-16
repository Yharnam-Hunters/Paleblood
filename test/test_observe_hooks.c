/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Observe hooks (runtime/observe_hooks.c) on a small function in a stand-in image: the observer gets
   the call's arguments, then the original runs from its relocated first instructions and returns its
   own result; an address a replacement takes is left alone. This program stands in for the host,
   exporting the renderer's record so the hooks are installed. Without Capstone nothing is hooked,
   and the test says so. */
#include "runtime/guest.h"
#include "runtime/host_hooks.h"
#include "runtime/observe_hooks.h"
#include "runtime/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the record the observer looks for in the host */
__attribute__((visibility("default"))) int rt_render_record_draw_auto(uint64_t buffer, uint32_t offset, uint32_t count, char *error,
                                                                      size_t error_size)
{
    (void)buffer, (void)offset, (void)count, (void)error, (void)error_size;
    return 0;
}

/* push rbp; mov rbp, rsp; push rbx; mov eax, edi; add eax, esi; add eax, edx; add eax, ecx; pop rbx;
   pop rbp; ret: returns the sum of its four arguments, its first 14 bytes whole instructions. */
static const unsigned char sum4[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x89, 0xf8, 0x01, 0xf0, 0x01, 0xd0, 0x01, 0xc8, 0x5b, 0x5d, 0xc3};
enum { FIRST = 0x1000, SECOND = 0x2000, IMAGE_SIZE = 0x4000 };

static uint64_t seen[6];
static int observed_calls;

static void observe(const uint64_t args[6])
{
    memcpy(seen, args, sizeof seen);
    observed_calls++;
}

static void never(const uint64_t args[6])
{
    (void)args;
    observed_calls += 100;
}

static void replaced_fn(void) {}

int main(void)
{
    if (!rt_trace_relocation_available()) {
        if (getenv("PB_REQUIRE_RELOCATION")) {fputs("required relocation unavailable\n",stderr);return 1;}
        puts("observe hooks: skipped (no Capstone: nothing is hooked)");
        return 0;
    }
#if defined(__APPLE__) && defined(__MACH__)
    CHECK(!rt_trace_available()); /* Observation does not imply a working single-step tracer. */
#endif
    unsigned char *image = mmap(NULL, IMAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(image != MAP_FAILED);
    if (image == MAP_FAILED) return 1;
    memcpy(image + FIRST, sum4, sizeof sum4);
    memcpy(image + SECOND, sum4, sizeof sum4);
    rt_host host = {.version = RT_HOST_API_VERSION, .image = image, .image_size = IMAGE_SIZE};
    CHECK(rt_host_hooks_initialize(&host) == 0);
    const rt_observe_hook table[] = {
        {RT_EBOOT_BASE + FIRST, sizeof sum4, "sum4", observe},
        {RT_EBOOT_BASE + SECOND, sizeof sum4, "replaced", never},
    };
    const rt_hook_entry replacements[] = {{RT_EBOOT_BASE + SECOND, sizeof sum4, "bb_replaced", replaced_fn, NULL}};
    CHECK(rt_observe_hooks_install(&host, table, 2, replacements, 1) == 1);

    int (*first)(int, int, int, int) = (int (*)(int, int, int, int))(void *)(image + FIRST);
    CHECK(first(1, 20, 300, 4000) == 4321);
    CHECK(observed_calls == 1 && seen[0] == 1 && seen[1] == 20 && seen[2] == 300 && seen[3] == 4000);
    CHECK(first(5, 6, 7, 8) == 26 && observed_calls == 2 && seen[3] == 8);
    /* the replaced address was not hooked to observe: its code is untouched */
    CHECK(memcmp(image + SECOND, sum4, sizeof sum4) == 0);

    if (!failures) puts("observe hooks: ok");
    return failures != 0;
}
