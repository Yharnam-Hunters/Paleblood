/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/capture_hooks.h"
#include "runtime/guest.h"
#include "runtime/host.h"
#include "runtime/hooks.h"
#include "runtime/observe_hooks.h"
#include "runtime/probe.h"

unsigned char *rt_image;

extern const rt_hook_entry game_hooks[];
extern const unsigned game_hook_count;
/* Function family members (tools/gen_hooks.py from symbols/families/), when the library has any. */
extern const rt_family_hook game_family_hooks[] __attribute__((weak));
extern const unsigned game_family_hook_count __attribute__((weak));
/* The game's observe hooks (runtime/observe_hooks.h), when the game library has any. */
extern const rt_observe_hook game_observe_hooks[] __attribute__((weak));
extern const unsigned game_observe_hook_count __attribute__((weak));

/* BB_HOOK_SKIP: comma-separated addresses or names of hooks to leave out (bisecting a problem
 * between replaced functions). */
static int skipped(const rt_hook_entry *h)
{
    const char *env = getenv("BB_HOOK_SKIP");
    char addr[16];
    if (!env || !*env)
        return 0;
    snprintf(addr, sizeof addr, "0x%08x", h->address);
    for (const char *p = env; *p;) {
        size_t n = strcspn(p, ",");
        if ((n == strlen(addr) && !strncmp(p, addr, n)) || (n == strlen(h->name) && !strncmp(p, h->name, n)))
            return 1;
        p += n + (p[n] == ',');
    }
    return 0;
}

/* BB_HOOK_SKIP may also name a family, or `families` for all of them. */
static int family_skipped(const rt_family_hook *h)
{
    const char *env = getenv("BB_HOOK_SKIP");
    char addr[16];
    if (!env || !*env)
        return 0;
    snprintf(addr, sizeof addr, "0x%08x", h->address);
    for (const char *p = env; *p;) {
        size_t n = strcspn(p, ",");
        if ((n == strlen(addr) && !strncmp(p, addr, n)) || (n == strlen(h->family) && !strncmp(p, h->family, n)) ||
            (n == strlen("families") && !strncmp(p, "families", n)))
            return 1;
        p += n + (p[n] == ',');
    }
    return 0;
}

static int install_family_hooks(const rt_host *host)
{
    if (!game_family_hooks || !&game_family_hook_count)
        return 0;
    unsigned installed = 0, skipped_count = 0;
    for (unsigned i = 0; i < game_family_hook_count; i++) {
        const rt_family_hook *h = &game_family_hooks[i];
        if (family_skipped(h)) {
            skipped_count++;
            continue;
        }
        if (h->address < RT_EBOOT_BASE || host->install_hook(h->address - RT_EBOOT_BASE, h->extent, (const void *)h->fn)) {
            fprintf(stderr, "bbgame: cannot hook family %s member at 0x%08x (%u bytes)\n", h->family, h->address, h->extent);
            return 1;
        }
        installed++;
    }
    if (installed || skipped_count)
        fprintf(stderr, "bbgame: %u family member hook(s) installed, %u skipped (BB_HOOK_SKIP)\n", installed, skipped_count);
    return 0;
}

int bbgame_init(const rt_host *host)
{
    if (!host || host->version != RT_HOST_API_VERSION) {
        fprintf(stderr, "bbgame: loader interface version %u, expected %u\n",
                host ? host->version : 0u, RT_HOST_API_VERSION);
        return 1;
    }
    rt_image = host->image;
    if (rt_hooks_install(game_hooks, game_hook_count))
        return 1;
    /* Observe hooks relocate the originals' first instructions, so they go in while those are intact. */
    if (game_observe_hooks && &game_observe_hook_count) {
        const int observed = rt_observe_hooks_install(host, game_observe_hooks, game_observe_hook_count, game_hooks, game_hook_count);
        if (observed) fprintf(stderr, "bbgame: %d function(s) observed for the renderer\n", observed);
    }
    /* With capture on, hooks that can be recorded jump to a recording thunk; the relocated copies
       of their first instructions must be taken now, before the hooks overwrite them. */
    const void **targets = calloc(game_hook_count ? game_hook_count : 1, sizeof *targets);
    if (!targets)
        return 1;
    rt_capture_hooks_prepare(host->image, host->image_size, game_hooks, game_hook_count, targets);
    unsigned installed = 0;
    for (unsigned i = 0; i < game_hook_count; i++) {
        const rt_hook_entry *h = &game_hooks[i];
        if (skipped(h)) {
            fprintf(stderr, "bbgame: %s (0x%08x) skipped (BB_HOOK_SKIP)\n", h->name, h->address);
            continue;
        }
        installed++;
        if (h->address < RT_EBOOT_BASE ||
            host->install_hook(h->address - RT_EBOOT_BASE, h->size, targets[i])) {
            fprintf(stderr, "bbgame: cannot hook %s at 0x%08x (%u bytes)\n", h->name, h->address, h->size);
            free(targets);
            return 1;
        }
    }
    free(targets);
    fprintf(stderr, "bbgame: %u hook(s) installed\n", installed);
    if (install_family_hooks(host))
        return 1;
    return rt_probes_install(host->image, host->image_size);
}
