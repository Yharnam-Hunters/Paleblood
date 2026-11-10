/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reserving address space, mapping aliases into it in a batch, unmapping across holes and releasing
   flexible memory (runtime/syslib/kernel_memory.c): sceKernelReserveVirtualRange,
   sceKernelBatchMap, sceKernelMunmap, sceKernelReleaseFlexibleMemory. */
#define _GNU_SOURCE
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

typedef size_t (*f_size)(void);
typedef int32_t (*f_alloc)(int64_t, int64_t, uint64_t, uint64_t, int32_t, int64_t *);
typedef int32_t (*f_map_direct)(void **, uint64_t, int32_t, int32_t, int64_t, uint64_t);
typedef int32_t (*f_release)(int64_t, uint64_t);
typedef int32_t (*f_map_flexible)(void **, uint64_t, int32_t, int32_t);
typedef int32_t (*f_munmap)(void *, uint64_t);
typedef int32_t (*f_reserve)(void **, uint64_t, int32_t, uint64_t);
typedef int32_t (*f_release_flexible)(void *, uint64_t);
typedef int32_t (*f_query)(const void *, int32_t, void *, uint64_t);
typedef int32_t (*f_protection)(const void *, void **, void **, int32_t *);

typedef struct {
    void *start;
    int64_t offset;
    uint64_t length;
    uint8_t protection, type;
    int16_t reserved;
    int32_t operation;
} entry;
typedef int32_t (*f_batch)(entry *, int32_t, int32_t *);

enum { BLOCK = 0x10000, BLOCKS = 3, PAGE = 0x4000, EINVAL_SCE = (int32_t)0x80020016u };

static f_reserve reserve;
static f_batch batch;
static f_munmap unmap;
static f_release_flexible release_flexible;
static void *reserved_space, *spare_space, *direct_map;
static int64_t pool_start;

/* Whether nothing is mapped at address any more: the host maps there again. */
static int is_unmapped(void *address)
{
    void *again = mmap(address, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (again != address) return 0;
    munmap(again, PAGE);
    return 1;
}

static void batch_one(void *start, int64_t offset, uint8_t protection, int32_t operation)
{
    entry e = {start, offset, BLOCK, protection, 0, 0, operation};
    batch(&e, 1, NULL);
}
static void reserve_fixed(void) { void *a = NULL; reserve(&a, BLOCK, 0x10, 0); }
static void reserve_hint(void) { void *a = (void *)0x10000000; reserve(&a, BLOCK, 0, 0); }
static void map_outside_reservation(void) { batch_one((char *)direct_map + BLOCK, pool_start, 3, 0); }
static void map_gpu_alias(void) { batch_one(spare_space, pool_start, 0x33, 0); }
static void map_no_protection(void) { batch_one(spare_space, pool_start, 0, 0); }
static void map_execute(void) { batch_one(spare_space, pool_start, 0x4, 0); }
static void map_unallocated(void) { batch_one(spare_space, 0x100000000ll, 3, 0); } /* 4 GiB into the pool: nothing there */
static void map_part_page(void)
{
    entry e = {spare_space, pool_start, PAGE / 2, 3, 0, 0, 0};
    batch(&e, 1, NULL);
}
static void unmap_unknown(void)
{
    static char local[PAGE] __attribute__((aligned(PAGE)));
    unmap(local, sizeof local);
}
static void map_flexible_op(void) { batch_one(spare_space, 0, 3, 3); }
static void type_protect_op(void) { batch_one(spare_space, 0, 3, 4); }
static void map_over_alias(void) { batch_one(reserved_space, pool_start, 3, 0); }
static void map_into_hole(void) { batch_one(reserved_space, pool_start, 3, 0); }
static void unmap_part(void) { unmap((char *)direct_map + PAGE, PAGE); }
static void unmap_off_page(void) { unmap((char *)reserved_space + 1, PAGE); }
static void release_direct(void) { release_flexible(direct_map, BLOCK * BLOCKS); }

SYSLIB_TEST(memory_reserve_batch)
{
    const f_size pool_size = (f_size)fn("libkernel", "sceKernelGetDirectMemorySize");
    const f_alloc allocate = (f_alloc)fn("libkernel", "sceKernelAllocateDirectMemory");
    const f_map_direct map_direct = (f_map_direct)fn("libkernel", "sceKernelMapDirectMemory");
    const f_release release = (f_release)fn("libkernel", "sceKernelReleaseDirectMemory");
    const f_map_flexible map_flexible = (f_map_flexible)fn("libkernel", "sceKernelMapFlexibleMemory");
    const f_query query = (f_query)fn("libkernel", "sceKernelVirtualQuery");
    reserve = (f_reserve)fn("libkernel", "sceKernelReserveVirtualRange");
    batch = (f_batch)fn("libkernel", "sceKernelBatchMap");
    unmap = (f_munmap)fn("libkernel", "sceKernelMunmap");
    release_flexible = (f_release_flexible)fn("libkernel", "sceKernelReleaseFlexibleMemory");
    CHECK(is_implemented("libkernel", "sceKernelReserveVirtualRange") && is_implemented("libkernel", "sceKernelBatchMap"));
    CHECK(is_implemented("libkernel", "sceKernelReleaseFlexibleMemory"));
    if (!pool_size || !allocate || !map_direct || !release || !map_flexible || !query || !reserve || !batch || !unmap ||
        !release_flexible)
        return;

    /* Reserving: shadPS4's EINVAL checks, then inaccessible space at an aligned address. */
    void *address = NULL;
    CHECK(reserve(NULL, BLOCK, 0x80, 0) == EINVAL_SCE);
    CHECK(reserve(&address, 0, 0x80, 0) == EINVAL_SCE && address == NULL);
    CHECK(reserve(&address, PAGE + 1, 0x80, 0) == EINVAL_SCE);
    CHECK(reserve(&address, BLOCK, 0x80, 3 * PAGE) == EINVAL_SCE);
    CHECK(reserve(&address, BLOCK, 0x80, PAGE / 2) == EINVAL_SCE);
    CHECK(reserve(&address, BLOCK, 0x80, 0x100000000ull) == EINVAL_SCE && address == NULL);
    CHECK(died_in_fatal(reserve_fixed));
    CHECK(died_in_fatal(reserve_hint));
    void *small = NULL;
    CHECK(reserve(&small, PAGE, 0, 0) == 0 && small); /* the next free address is now off any large alignment */
    CHECK(reserve(&address, BLOCK * BLOCKS, 0x80, 0x1000000) == 0 && address && (uintptr_t)address % 0x1000000 == 0);
    CHECK(unmap(small, PAGE) == 0 && is_unmapped(small));
    CHECK(!is_unmapped(address)); /* reserved: the host keeps it */
    reserved_space = address;

    /* The blocks, mapped once by the system, then aliased into the reserved space in one batch,
       each at its start, as the executable's block allocator does. */
    CHECK(allocate(0, (int64_t)pool_size(), BLOCK * BLOCKS, BLOCK, 0, &pool_start) == 0);
    CHECK(map_direct(&direct_map, BLOCK * BLOCKS, 3, 0, pool_start, BLOCK) == 0 && direct_map);
    entry entries[BLOCKS];
    for (int i = 0; i < BLOCKS; i++) entries[i] = (entry){(char *)address + i * BLOCK, pool_start + i * BLOCK, BLOCK, 3, 0, 0, 0};
    int32_t processed = -1;
    CHECK(batch(entries, BLOCKS, &processed) == 0 && processed == BLOCKS);
    for (int i = 0; i < BLOCKS; i++) {
        ((volatile uint8_t *)direct_map)[i * BLOCK + 5] = (uint8_t)(0x40 + i);
        CHECK(((volatile uint8_t *)address)[i * BLOCK + 5] == 0x40 + i); /* the same memory */
    }
    uint8_t info[0x48];
    int64_t offset;
    CHECK(query((char *)address + BLOCK, 0, info, sizeof info) == 0);
    memcpy(&offset, info + 0x10, sizeof offset);
    CHECK(offset == pool_start + BLOCK);
    CHECK(reserve(&spare_space, BLOCK, 0, 0) == 0 && spare_space);
    CHECK(died_in_fatal(map_outside_reservation));
    CHECK(died_in_fatal(map_over_alias)); /* mapped now, no longer reserved */
    CHECK(died_in_fatal(map_execute));
    CHECK(died_in_fatal(map_part_page));
    CHECK(died_in_fatal(unmap_unknown));
    CHECK(died_in_fatal(map_gpu_alias));
    CHECK(died_in_fatal(map_no_protection));
    CHECK(died_in_fatal(map_unallocated));
    CHECK(died_in_fatal(map_flexible_op));
    CHECK(died_in_fatal(type_protect_op));
    CHECK(died_in_fatal(unmap_part));
    CHECK(died_in_fatal(unmap_off_page));

    /* Protect (2) is sceKernelMprotect on one alias. */
    entries[1].operation = 2;
    entries[1].protection = 1;
    CHECK(batch(&entries[1], 1, &processed) == 0 && processed == 1);
    const f_protection protection_of = (f_protection)fn("libkernel", "sceKernelQueryMemoryProtection");
    int32_t protection = -1;
    CHECK(protection_of && protection_of(entries[1].start, NULL, NULL, &protection) == 0 && protection == 1);

    /* Freeing: unmap every entry, then the whole range, which is all holes by then. */
    for (int i = 0; i < BLOCKS; i++) entries[i].operation = 1;
    CHECK(batch(entries, BLOCKS, NULL) == 0);
    for (int i = 0; i < BLOCKS; i++) CHECK(is_unmapped((char *)address + i * BLOCK));
    CHECK(unmap(address, BLOCK * BLOCKS) == 0);
    CHECK(((volatile uint8_t *)direct_map)[5] == 0x40); /* the system's own map is untouched */

    /* A failed batch stops at the bad entry and says how many were done; the executable then
       unmaps every entry, mapped or not, and the reserved range across the holes. */
    address = NULL;
    CHECK(reserve(&address, BLOCK * BLOCKS, 0x80, 0) == 0);
    for (int i = 0; i < BLOCKS; i++) entries[i] = (entry){(char *)address + i * BLOCK, pool_start + i * BLOCK, BLOCK, 3, 0, 0, 0};
    entries[1].length = 0;
    CHECK(batch(entries, BLOCKS, &processed) == EINVAL_SCE && processed == 1);
    entries[1].length = BLOCK;
    entries[2].operation = 5;
    CHECK(batch(&entries[2], 1, &processed) == EINVAL_SCE && processed == 0);
    CHECK(batch(NULL, 1, &processed) == EINVAL_SCE && processed == 0);
    CHECK(batch(entries, 0, &processed) == 0 && processed == 0);
    for (int i = 0; i < BLOCKS; i++) entries[i].operation = 1;
    CHECK(batch(entries, BLOCKS, NULL) == 0);
    CHECK(unmap(address, BLOCK * BLOCKS) == 0);
    CHECK(is_unmapped(address) && is_unmapped((char *)address + 2 * BLOCK));

    /* Reserved space unmapped in part: the rest stays reserved and can still be mapped into. */
    address = NULL;
    CHECK(reserve(&address, BLOCK * 2, 0x80, 0) == 0);
    CHECK(unmap(address, BLOCK) == 0 && is_unmapped(address));
    reserved_space = address;
    entries[0] = (entry){(char *)address + BLOCK, pool_start, BLOCK, 3, 0, 0, 0};
    CHECK(batch(entries, 1, NULL) == 0 && ((volatile uint8_t *)address)[BLOCK + 5] == 0x40);
    CHECK(died_in_fatal(map_into_hole)); /* the first half is a hole now, not reserved */
    CHECK(unmap(address, BLOCK * 2) == 0 && is_unmapped((char *)address + BLOCK));
    /* The other way round: the first half stays reserved when the second goes. */
    address = NULL;
    CHECK(reserve(&address, BLOCK * 2, 0x80, 0) == 0);
    CHECK(unmap((char *)address + BLOCK, BLOCK) == 0 && is_unmapped((char *)address + BLOCK));
    entries[0] = (entry){address, pool_start, BLOCK, 3, 0, 0, 0};
    CHECK(batch(entries, 1, NULL) == 0 && ((volatile uint8_t *)address)[5] == 0x40);
    CHECK(unmap(address, BLOCK * 2) == 0 && is_unmapped(address));
    CHECK(unmap(spare_space, BLOCK) == 0);

    /* Every alias gone, the direct range is released once its own map goes too. */
    CHECK(died_in_fatal(release_direct));
    CHECK(unmap(direct_map, BLOCK * BLOCKS) == 0);
    CHECK(release(pool_start, BLOCK * BLOCKS) == 0);

    /* Releasing flexible memory unmaps it (the maintainer's ruling). */
    void *flexible = NULL;
    CHECK(map_flexible(&flexible, BLOCK, 3, 0) == 0 && flexible);
    CHECK(release_flexible(NULL, BLOCK) == EINVAL_SCE);
    CHECK(release_flexible(flexible, 0) == EINVAL_SCE);
    CHECK(release_flexible(flexible, PAGE + 1) == EINVAL_SCE);
    CHECK(!is_unmapped(flexible));
    CHECK(release_flexible(flexible, BLOCK) == 0 && is_unmapped(flexible));
}
