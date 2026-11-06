/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Observe hooks (runtime/observe_hooks.h). Each installed hook gets a relocated copy of the
   original's first instructions (ending in a jump back to the rest of it) and a thunk the hook jumps
   to: movabs r11, hook; jmp rt_observe_entry, which calls the observer and then the relocated copy. */
#include "runtime/observe_hooks.h"

#include "runtime/capture_hooks.h"
#include "runtime/guest.h"
#include "runtime/render_observer.h"
#include "runtime/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum { HOOK_JUMP = 14, THUNK_SIZE = 32, MAX_OBSERVE_HOOKS = 64 };

/* What rt_observe_entry jumps through: the relocated entry first. */
typedef struct {
    const void *relocated;
    rt_observer_fn observe;
} observed_hook;

static observed_hook observed[MAX_OBSERVE_HOOKS];

void rt_observe_entry(void);
void rt_observe_dispatch(const observed_hook *hook, const uint64_t *frame);

/* The frame rt_observe_entry saved: rdi, rsi, rdx, rcx, r8, r9 first. */
void rt_observe_dispatch(const observed_hook *hook, const uint64_t *frame)
{
    hook->observe(frame);
}

static int replaced(uint32_t address, const rt_hook_entry *table, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (table[i].address == address) return 1;
    return 0;
}

int rt_observe_hooks_install(const rt_host *host, const rt_observe_hook *table, size_t count, const rt_hook_entry *replaced_table,
                             size_t replaced_count)
{
    if (!count || count > MAX_OBSERVE_HOOKS || !rt_trace_available() || !rt_render_observer_available()) return 0;
    const size_t code_size = (count * (RT_RELOCATE_MAX + THUNK_SIZE) + 0xfff) & ~(size_t)0xfff;
    unsigned char *code = rt_map_near_image(host->image, host->image_size, code_size);
    if (!code) {
        fprintf(stderr, "bbgame: no memory near the image for observe hooks; none installed\n");
        return 0;
    }
    unsigned char *entries = code, *thunks = code + count * RT_RELOCATE_MAX;
    const void *targets[MAX_OBSERVE_HOOKS] = {0};
    for (size_t i = 0; i < count; i++) {
        const rt_observe_hook *h = &table[i];
        if (replaced(h->address, replaced_table, replaced_count) || h->address < RT_EBOOT_BASE || h->size < HOOK_JUMP) continue;
        unsigned char *original = host->image + (h->address - RT_EBOOT_BASE), *entry = entries + i * RT_RELOCATE_MAX;
        size_t copied, written;
        if (rt_trace_relocate(original, HOOK_JUMP, entry, &copied, &written) || rt_trace_branches_into(original, h->size, copied)) {
            fprintf(stderr, "bbgame: %s (0x%08x) cannot be observed: its first instructions do not move\n", h->name, h->address);
            continue;
        }
        observed[i] = (observed_hook){entry, h->observe};
        /* movabs r11, &observed[i]; jmp qword ptr [rip]; .quad rt_observe_entry */
        unsigned char *t = thunks + i * THUNK_SIZE;
        const uint64_t hook = (uint64_t)(uintptr_t)&observed[i], target = (uint64_t)(uintptr_t)rt_observe_entry;
        t[0] = 0x49, t[1] = 0xbb;
        memcpy(t + 2, &hook, 8);
        const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0};
        memcpy(t + 10, jump, sizeof jump);
        memcpy(t + 16, &target, 8);
        targets[i] = t;
    }
    if (mprotect(code, code_size, PROT_READ | PROT_EXEC)) {
        fprintf(stderr, "bbgame: cannot make the observe hooks executable; none installed\n");
        return 0;
    }
    int installed = 0;
    for (size_t i = 0; i < count; i++) {
        if (!targets[i]) continue;
        if (host->install_hook(table[i].address - RT_EBOOT_BASE, table[i].size, targets[i])) {
            fprintf(stderr, "bbgame: cannot hook %s (0x%08x) to observe it\n", table[i].name, table[i].address);
            continue;
        }
        installed++;
    }
    return installed;
}
