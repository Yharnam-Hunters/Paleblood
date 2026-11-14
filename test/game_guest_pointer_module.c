/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A shared library with the real plugin initializer and bridge, but only an owned hook. */
#include "runtime/game_guest_pointer.h"
#include "runtime/capture_hooks.h"
#include "runtime/observe_hooks.h"
#include "runtime/probe.h"
#include "runtime/guest.h"
#include "runtime/hooks.h"
#include "runtime/host.h"

static void owned_replacement(void) {}
const rt_hook_entry game_hooks[] = {
    {RT_EBOOT_BASE, 16, "owned_guest_pointer", owned_replacement, 0},
};
const unsigned game_hook_count = 1;
/* Owned empty registries resolve Mach-O module references without pending runtime archives. */
const rt_family_hook game_family_hooks[] = {{0}};
const unsigned game_family_hook_count = 0;
const rt_observe_hook game_observe_hooks[] = {{0}};
const unsigned game_observe_hook_count = 0;

/* The real initializer and hook registry use these owned peripheral seams to expose the
   call order and the normal no-capture/no-probe targets. */
static unsigned peripheral_calls;
unsigned owned_bridge_peripheral_calls(void) { return peripheral_calls; }
int rt_capture_original_mode(void) { ++peripheral_calls; return 0; }
int rt_capture_hooks_prepare(unsigned char *image,uint64_t image_size,const rt_hook_entry *table,
                             size_t count,const void **targets)
{
    (void)image;(void)image_size;++peripheral_calls;
    for (size_t i=0;i<count;++i) targets[i]=(const void *)table[i].fn;
    return 0;
}
int rt_probes_install(unsigned char *image,uint64_t image_size)
{
    (void)image;(void)image_size;++peripheral_calls;return 0;
}
int rt_observe_hooks_install(const rt_host *host,const rt_observe_hook *table,size_t count,
                             const rt_hook_entry *replaced,size_t replaced_count)
{
    (void)host;(void)table;(void)count;(void)replaced;(void)replaced_count;
    ++peripheral_calls;return 0;
}

int owned_bridge_initialize(const rt_host *host) { return bbgame_init(host); }
unsigned char *owned_bridge_pointer(void) { return rt_game_guest_thread_pointer(); }

#if defined(PB_WRONG_SLOT_CONTROL)
/* One compiled wrong component: substitute another owned public TSD slot for the canonical
   provider. The host qualifies that slot with live witnesses before the decisive comparison. */
static uint32_t wrong_offset;
static uint32_t owned_wrong_offset(void) { return wrong_offset; }
void owned_bridge_wrong_slot(uint32_t offset)
{
    wrong_offset = offset;
    rt_game_guest_offset_provider = owned_wrong_offset;
}
#endif
