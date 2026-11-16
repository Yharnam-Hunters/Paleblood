/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "runtime/boot_arena.h"
#include "runtime/memory.h"
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

static int rounded_size(uint64_t size, size_t *out)
{
    const long page=sysconf(_SC_PAGESIZE);
    if (page<=0 || !size || size>SIZE_MAX-((size_t)page-1)) {errno=EINVAL;return -1;}
    *out=((size_t)size+(size_t)page-1)/(size_t)page*(size_t)page;
    return 0;
}
int rt_boot_arena_create(rt_boot_arena *arena,uint64_t size)
{
    if (!arena || arena->base || arena->size) {errno=EINVAL;return -1;}
    size_t rounded;
    if (rounded_size(size,&rounded)) return -1;
    /* Host placement policy, not a console layout: keep the legacy image base when possible.
       Darwin can occupy that base and the lower aligned candidates before startup. */
    static const uint64_t candidates[]={UINT64_C(0x800000000),UINT64_C(0x2000000000),
        UINT64_C(0x4000000000),UINT64_C(0x6000000000),UINT64_C(0x8000000000),
        UINT64_C(0xa000000000),UINT64_C(0xc000000000)};
    const uint64_t limit=UINT64_C(0xe000000000);
    for (size_t i=0;i<sizeof candidates/sizeof candidates[0];++i) {
        if (rounded>limit-candidates[i]) continue;
        void *base=rt_map_anonymous_at((void *)(uintptr_t)candidates[i],rounded,PROT_NONE);
        if (base!=MAP_FAILED) {*arena=(rt_boot_arena){base,rounded};return 0;}
        if (errno!=EEXIST && errno!=EADDRNOTAVAIL) return -1;
    }
    errno=EADDRNOTAVAIL;return -1;
}
int rt_boot_arena_protect(const rt_boot_arena *arena,uint64_t offset,uint64_t size,int protection)
{
    const long page=sysconf(_SC_PAGESIZE);size_t rounded;
    if (!arena || !arena->base || !arena->size || page<=0 || offset%(size_t)page ||
        offset>=arena->size || (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC))) {errno=EINVAL;return -1;}
    if (rounded_size(size,&rounded)) return -1;
    if (rounded>arena->size-offset) {errno=EINVAL;return -1;}
    return mprotect((unsigned char *)arena->base+(size_t)offset,rounded,protection);
}
int rt_boot_arena_release(rt_boot_arena *arena)
{
    if (!arena) {errno=EINVAL;return -1;}
    if (!arena->base && !arena->size) return 0;
    if (!arena->base || !arena->size) {errno=EINVAL;return -1;}
    if (munmap(arena->base,arena->size)) return -1;
    *arena=(rt_boot_arena){0};return 0;
}
