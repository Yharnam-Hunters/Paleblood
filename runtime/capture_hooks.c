/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Recording hooked calls by tracing their originals (docs/capture-architecture.md).

   At install time, while the original code is still intact, each hook that can be recorded gets:
   - its original's first instructions, relocated into a block near the image (the hook's jump
     overwrites them), followed by a jump back into the rest of the original;
   - a thunk, which becomes the hook's target: it loads the hook's record and enters
     rt_capture_entry (runtime/capture_entry.S);
   - a redirect, so traced code that reaches this original's entry continues in the relocated
     copy instead of the hook's jump.
   At run time rt_capture_dispatch decides each call: unsampled calls (and calls made while this
   thread is already tracing) go on to the replacement unchanged; a sampled call runs the original
   under the tracer and returns its result, and its case is written when the trace is complete.

   A hook is recorded only when capture is on (BB_CAPTURE_DIR), tracing is asked for
   (BB_CAPTURE_TRACE=1: a traced call runs one instruction at a time, far slower than the game
   expects), the runtime has the tracer, the hook table gives a return class, the entry
   relocates, and no branch in the function jumps back into its first bytes. Every other hook
   jumps straight to its replacement, as before. A function whose traces keep giving no case
   (GIVE_UP in a row) is not traced again in this run. */
#define _GNU_SOURCE
#include "runtime/capture_hooks.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include "runtime/capture.h"
#include "runtime/capture_trace.h"
#include "runtime/guest.h"
#include "runtime/trace.h"

enum { HOOK_JUMP = 14, THUNK_SIZE = 32, MAX_HOOKS = 1024, REACH = 0x70000000, GIVE_UP = 3 };

/* rt_capture_frame: the layout runtime/capture_entry.S builds on the stack. */
typedef struct {
    uint64_t gpr[6];
    uint64_t rax, hook, caller_rsp, pad;
    uint8_t xmm[8][16];
    uint64_t ret_rax, ret_rdx;
    uint8_t ret_xmm0[16], ret_xmm1[16];
} rt_capture_frame;
_Static_assert(__builtin_offsetof(rt_capture_frame, xmm) == 80, "frame layout");
_Static_assert(__builtin_offsetof(rt_capture_frame, ret_rax) == 208, "frame layout");
_Static_assert(sizeof(rt_capture_frame) == 256, "frame layout");

typedef struct {
    rt_hook_fn replacement;           /* first: rt_capture_entry reads it at offset 0 */
    char name[128];
    rt_traced_function traced;
    atomic_int failed;                /* traces in a row that gave no case */
} capture_hook;

void rt_capture_entry(void);

static capture_hook *hooks;
static size_t hook_count;
static rt_trace_redirect *redirects;
static size_t redirect_count;
static unsigned char *code_block;     /* relocated entries, then thunks */
static size_t code_size;
static rt_code_range image_range, code_range;

/* A block within REACH of the image, so relocated RIP-relative operands still reach their data. */
static unsigned char *map_near(unsigned char *image, uint64_t image_size, size_t size)
{
    const uintptr_t lo = (uintptr_t)image, hi = (uintptr_t)image + image_size;
    for (uintptr_t step = 1u << 24; step < REACH; step <<= 1) {
        const uintptr_t hints[2] = {(hi + step) & ~(uintptr_t)0xfff, lo > step + size ? (lo - step - size) & ~(uintptr_t)0xfff : 0};
        for (int i = 0; i < 2; i++) {
            if (!hints[i]) continue;
            void *p = mmap((void *)hints[i], size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) continue;
            const uintptr_t a = (uintptr_t)p;
            if ((a >= hi && a + size - lo < REACH) || (a + size <= lo && hi - a < REACH)) return p;
            munmap(p, size);
        }
    }
    return NULL;
}

static int compare_redirect(const void *a, const void *b)
{
    const uintptr_t x = (uintptr_t)((const rt_trace_redirect *)a)->from, y = (uintptr_t)((const rt_trace_redirect *)b)->from;
    return x < y ? -1 : x > y;
}

static int capture_requested(void)
{
    const char *dir = getenv("BB_CAPTURE_DIR"), *trace = getenv("BB_CAPTURE_TRACE");
    return dir && *dir && trace && !strcmp(trace, "1") && rt_trace_available();
}

int rt_capture_hooks_prepare(unsigned char *image, uint64_t image_size, const rt_hook_entry *table, size_t count,
                             const void **targets)
{
    for (size_t i = 0; i < count; i++) targets[i] = (const void *)table[i].fn;
    if (!capture_requested() || !count || count > MAX_HOOKS) return 0;

    code_size = (count * (RT_RELOCATE_MAX + THUNK_SIZE) + 0xfff) & ~(size_t)0xfff;
    code_block = map_near(image, image_size, code_size);
    hooks = calloc(count, sizeof *hooks);
    redirects = calloc(count, sizeof *redirects);
    if (!code_block || !hooks || !redirects) {
        fprintf(stderr, "capture: no memory for recording hooked calls; nothing is recorded\n");
        return 0;
    }
    unsigned char *entries = code_block, *thunks = code_block + count * RT_RELOCATE_MAX;
    size_t recorded = 0;
    for (size_t i = 0; i < count; i++) {
        const rt_hook_entry *h = &table[i];
        unsigned char *original = image + (h->address - RT_EBOOT_BASE);
        unsigned char *entry = entries + i * RT_RELOCATE_MAX;
        size_t copied, written;
        if (!h->returns || h->address < RT_EBOOT_BASE || h->size < HOOK_JUMP ||
            rt_trace_relocate(original, HOOK_JUMP, entry, &copied, &written) ||
            rt_trace_branches_into(original, h->size, copied))
            continue;
        capture_hook *c = &hooks[hook_count++];
        c->replacement = h->fn;
        /* Cases are filed under the function's name: the replacement's without its bb_ prefix. */
        snprintf(c->name, sizeof c->name, "%s", strncmp(h->name, "bb_", 3) ? h->name : h->name + 3);
        c->traced = (rt_traced_function){c->name, h->address, h->returns, entry};
        /* movabs r11, c; jmp qword ptr [rip]; .quad rt_capture_entry */
        unsigned char *t = thunks + i * THUNK_SIZE;
        const uint64_t record = (uint64_t)(uintptr_t)c, target = (uint64_t)(uintptr_t)rt_capture_entry;
        t[0] = 0x49, t[1] = 0xbb;
        memcpy(t + 2, &record, 8);
        const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0};
        memcpy(t + 10, jump, sizeof jump);
        memcpy(t + 16, &target, 8);
        targets[i] = t;
        redirects[redirect_count++] = (rt_trace_redirect){original, entry};
        recorded++;
    }
    if (mprotect(code_block, code_size, PROT_READ | PROT_EXEC)) {
        for (size_t i = 0; i < count; i++) targets[i] = (const void *)table[i].fn;
        hook_count = redirect_count = 0;
        fprintf(stderr, "capture: cannot make the relocated entries executable; nothing is recorded\n");
        return 0;
    }
    qsort(redirects, redirect_count, sizeof *redirects, compare_redirect);
    rt_trace_set_redirects(redirects, redirect_count);
    image_range = (rt_code_range){image, image + image_size};
    code_range = (rt_code_range){code_block, code_block + count * RT_RELOCATE_MAX};
    fprintf(stderr, "capture: %zu of %zu hooked function(s) can be recorded\n", recorded, count);
    return (int)recorded;
}

/* The caller's stack arguments, as far as its stack is readable. */
static void copy_stack(uint64_t caller_rsp, uint64_t *out)
{
    memset(out, 0, sizeof(uint64_t) * RT_TRACE_STACK_SLOTS);
    for (size_t n = RT_TRACE_STACK_SLOTS; n > 0; n--) {
        struct iovec local = {out, n * 8}, remote = {(void *)(uintptr_t)(caller_rsp + 8), n * 8};
        if (syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0) == (long)(n * 8)) return;
    }
}

int rt_capture_dispatch(capture_hook *c, rt_capture_frame *f)
{
    if (rt_trace_active() || atomic_load(&c->failed) >= GIVE_UP) return 0;
    const int number = rt_capture_begin(c->name);
    if (number < 0) return 0;
    rt_trace_args args;
    memcpy(args.gpr, f->gpr, sizeof args.gpr);
    memcpy(args.xmm, f->xmm, sizeof args.xmm);
    copy_stack(f->caller_rsp, args.stack);
    const rt_code_range code[2] = {image_range, code_range};
    rt_trace_result result;
    const int written = rt_capture_traced_call(&c->traced, number, &args, image_range, code, 2, &result);
    if (written < 0) return 0;
    if (written) {
        atomic_store(&c->failed, 0);
    } else if (atomic_fetch_add(&c->failed, 1) + 1 == GIVE_UP) {
        fprintf(stderr, "capture: %s: %d traces in a row gave no case; not traced again in this run "
                        "(BB_CAPTURE_VERBOSE=1 says why)\n", c->name, GIVE_UP);
    }
    f->ret_rax = result.rax;
    f->ret_rdx = result.rdx;
    memcpy(f->ret_xmm0, result.xmm0, 16);
    memcpy(f->ret_xmm1, result.xmm1, 16);
    return 1;
}
