/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_BOOT_ARENA_H
#define RUNTIME_BOOT_ARENA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Native-owned image arena below the runtime's 40-bit map limit. Zero-initialize; only these
   functions may change ownership fields. The preferred legacy image base is tried first, then
   aligned higher candidates. A declined hint does not prove occupancy. No host map is replaced. */
typedef struct { void *base; size_t size; } rt_boot_arena;
int rt_boot_arena_create(rt_boot_arena *arena, uint64_t size);
/* Change protection only inside the still-owned reservation. Offset must be host-page aligned;
   nonzero size is rounded to pages and must fit completely. No MAP_FIXED or unmap/remap gap. */
int rt_boot_arena_protect(const rt_boot_arena *arena, uint64_t offset, uint64_t size, int protection);
/* Quiesce all guest/direct readers before releasing. Failure retains ownership for retry.
   The CLI retains image arenas for process lifetime because workers can outlive entry. */
int rt_boot_arena_release(rt_boot_arena *arena);
#ifdef __cplusplus
}
#endif
#endif
