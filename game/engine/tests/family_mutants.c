/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Engine-specific deliberately wrong replacements for the engine function families
   (game/engine/family_cases.py MUTANTS), built into libbbfamily_mutants beside the generic ones
   (test/family_mutants.c), whose bbgame_init records the image these reach through rt_guest. */
#include <stdint.h>

#include "runtime/guest.h"

/* The guard of the static wide string the initialize_static_wide_string members build. */
#define STATIC_WIDE_STRING_GUARD 0x05acebc8u

/* Publishes the guard and does nothing else: wrong for initialize_static_wide_string, which
   builds the string and registers its destructor first on a clear guard and leaves a set guard
   alone. */
void bb_mutant_static_wide_string_guard_only(void)
{
    *(volatile uint64_t *)rt_guest(STATIC_WIDE_STRING_GUARD) = 1;
}

/* frees_kind_sized_dword_storage_through_0x05ac8288 (game/engine/kind_sized_dword_free_family.cpp):
   an object with a vtable whose slot 0x88 reports the kind, a buffer at +0x8 and the count at
   +0x18. Each mutant below gets one detail wrong; the controls move one read across the kind
   calls. */
#define DWORD_FREE_SIZED_FREE 0x02fa1c20u
#define DWORD_FREE_ALLOCATOR 0x05ac8288u
#define DWORD_FREE_KIND_SLOT 17
#define DWORD_FREE_THREE_PER_ENTRY 4u
#define DWORD_FREE_THREE_PER_ENTRY_AND_TWO 6u
#define DWORD_FREE_PER_ENTRY 3u
#define DWORD_FREE_EXTRA 2u
#define DWORD_FREE_SIZE 4u

struct dword_storage;
typedef int32_t (*dword_storage_kind)(struct dword_storage *, int32_t);
typedef void (*dword_storage_sized_free)(void *, void *, uint64_t, uint64_t);

struct dword_storage {
    void **vtable;
    void *storage;
    void *unknown_0x10;
    int32_t count;
};

/* What the mutant gets wrong. */
#define DWORD_FREE_ALLOCATOR_LATE 1  /* reads the allocator after the kind calls */
#define DWORD_FREE_COUNT_EARLY 2  /* reads the count before the kind calls */
#define DWORD_FREE_ASKS_ONCE 4  /* uses the first answer for the kind 6 test too */
#define DWORD_FREE_CACHES_VTABLE 8  /* calls the second time through the first vtable */
#define DWORD_FREE_NO_ENTRIES 16  /* leaves the entries out of the size */

static void *dword_storage_allocator(void)
{
    return *(void *volatile *)rt_guest(DWORD_FREE_ALLOCATOR);
}

static uint32_t dword_storage_ask(struct dword_storage *object, void **vtable)
{
    return (uint32_t)((dword_storage_kind)vtable[DWORD_FREE_KIND_SLOT])(object, 0) & ~1u;
}

static void dword_storage_free_with(struct dword_storage *object, int wrong)
{
    volatile int32_t *field = &object->count;
    void *allocator = dword_storage_allocator();
    int32_t early = *field;
    void **vtable = object->vtable;
    int32_t count;
    int32_t elements;
    uint32_t first = dword_storage_ask(object, vtable);
    if (first == DWORD_FREE_THREE_PER_ENTRY) {
        count = (wrong & DWORD_FREE_COUNT_EARLY) ? early : *field;
        elements = (int32_t)((uint32_t)count * DWORD_FREE_PER_ENTRY);
    } else {
        uint32_t second = dword_storage_ask(object, (wrong & DWORD_FREE_CACHES_VTABLE) ? vtable : object->vtable);
        if (wrong & DWORD_FREE_ASKS_ONCE)
            second = first;
        count = (wrong & DWORD_FREE_COUNT_EARLY) ? early : *field;
        elements = second == DWORD_FREE_THREE_PER_ENTRY_AND_TWO
                       ? (int32_t)((uint32_t)count * DWORD_FREE_PER_ENTRY + DWORD_FREE_EXTRA) : count;
    }
    if (wrong & DWORD_FREE_ALLOCATOR_LATE)
        allocator = dword_storage_allocator();
    int64_t dwords = (int64_t)elements + ((wrong & DWORD_FREE_NO_ENTRIES) ? 0 : (int64_t)count);
    ((dword_storage_sized_free)rt_guest(DWORD_FREE_SIZED_FREE))(allocator, object->storage,
                                                               (uint64_t)dwords * DWORD_FREE_SIZE, DWORD_FREE_SIZE);
}

void bb_mutant_dword_storage_free_reads_allocator_late(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_ALLOCATOR_LATE);
}

void bb_mutant_dword_storage_free_reads_count_early(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_COUNT_EARLY);
}

void bb_mutant_dword_storage_free_asks_once(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_ASKS_ONCE);
}

void bb_mutant_dword_storage_free_caches_vtable(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_CACHES_VTABLE);
}

void bb_mutant_dword_storage_free_without_entries(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_NO_ENTRIES);
}

/* deletes_work_holder (game/engine/work_holder_delete_family.cpp): a holder (vtable, work at +0x8)
   of shared work (reference count +0x8, signal +0x10, users +0x30). */
#define WORK_HOLDER_BASE_VTABLE 0x057b1a80u
#define WORK_HOLDER_SIGNAL_SET 0x0247e780u
#define WORK_HOLDER_FATAL_ERROR 0x024b55b0u
#define WORK_HOLDER_DELETE 0x02fbe588u
#define WORK_HOLDER_INVALID_UNREF 0x04d40331u
#define WORK_HOLDER_UNREF_LINE 0x3e

struct mutant_work {
    void (**vtable)(struct mutant_work *);
    int32_t references;
    int32_t unknown_0x0c;
    uint8_t unknown_0x10[0x20];  /* the signal */
    int32_t users;
};
struct mutant_work_holder {
    const void *vtable;
    struct mutant_work *work;
};
typedef uint64_t (*work_holder_signal_set)(void *, uint8_t);
typedef void (*work_holder_fatal_error)(const char *, int32_t, const char *, ...);
typedef void (*work_holder_delete)(void *);

#define WORK_HOLDER_RIGHT 0
#define WORK_HOLDER_WORK_ONCE 1  /* does not read the work again after the signal */
#define WORK_HOLDER_SIGNAL_ON_ONE 2  /* sets the signal only when the users were exactly 1 */
#define WORK_HOLDER_KEPT 4  /* does not delete the holder */

static void work_holder_delete_with(struct mutant_work_holder *holder, int wrong)
{
    holder->vtable = rt_guest(WORK_HOLDER_BASE_VTABLE);
    struct mutant_work *work = holder->work;
    if (work) {
        int32_t users = __atomic_fetch_sub(&work->users, 1, __ATOMIC_SEQ_CST);
        if ((wrong & WORK_HOLDER_SIGNAL_ON_ONE) ? users == 1 : users <= 1)
            ((work_holder_signal_set)rt_guest(WORK_HOLDER_SIGNAL_SET))(work->unknown_0x10, 1);
        if (!(wrong & WORK_HOLDER_WORK_ONCE))
            work = *(struct mutant_work *volatile *)&holder->work;
        if (work) {
            int32_t references = __atomic_fetch_sub(&work->references, 1, __ATOMIC_SEQ_CST);
            if (references == 1)
                work->vtable[0](work);
            else if (references <= 0)
                ((work_holder_fatal_error)rt_guest(WORK_HOLDER_FATAL_ERROR))(
                    0, WORK_HOLDER_UNREF_LINE, (const char *)rt_guest(WORK_HOLDER_INVALID_UNREF));
        }
    }
    if (!(wrong & WORK_HOLDER_KEPT))
        ((work_holder_delete)rt_guest(WORK_HOLDER_DELETE))(holder);
}

void bb_mutant_work_holder_delete_work_once(struct mutant_work_holder *holder)
{
    work_holder_delete_with(holder, WORK_HOLDER_WORK_ONCE);
}

void bb_mutant_work_holder_delete_signal_on_one(struct mutant_work_holder *holder)
{
    work_holder_delete_with(holder, WORK_HOLDER_SIGNAL_ON_ONE);
}

void bb_mutant_work_holder_delete_kept(struct mutant_work_holder *holder)
{
    work_holder_delete_with(holder, WORK_HOLDER_KEPT);
}

/* creates_0xe40_byte_object_with_0x020e9820 (game/engine/object_create_family.cpp): the owner's
   first field is an allocator whose slot 0x58 allocates. */
#define CREATE_OBJECT_SIZE 0xe40u
#define CREATE_OBJECT_ALIGNMENT 0x10u
#define CREATE_WRONG_ALIGNMENT 8u
#define CREATE_ALLOCATE_SLOT 11
#define CREATE_CONSTRUCT 0x020e9820u

typedef void *(*create_allocate)(void *, uint64_t, uint64_t);
typedef void (*create_construct)(void *, void *);

static void *create_with(void ***owner, uint64_t alignment, int construct_always)
{
    void **allocator = *owner;
    void *object = ((create_allocate)(*(void ***)allocator)[CREATE_ALLOCATE_SLOT])(allocator, CREATE_OBJECT_SIZE, alignment);
    if (object || construct_always)
        ((create_construct)rt_guest(CREATE_CONSTRUCT))(object, owner);
    return object;
}

/* Allocates with alignment 8 instead of 16. */
void *bb_mutant_create_object_aligned_8(void ***owner)
{
    return create_with(owner, CREATE_WRONG_ALIGNMENT, 0);
}

/* Constructs even when the allocation failed. */
void *bb_mutant_create_object_constructs_on_failure(void ***owner)
{
    return create_with(owner, CREATE_OBJECT_ALIGNMENT, 1);
}

/* Returns what the constructor returns instead of the object. */
void *bb_mutant_create_object_returns_constructor_result(void ***owner)
{
    void **allocator = *owner;
    void *object = ((create_allocate)(*(void ***)allocator)[CREATE_ALLOCATE_SLOT])(allocator, CREATE_OBJECT_SIZE,
                                                                                    CREATE_OBJECT_ALIGNMENT);
    if (!object)
        return 0;
    return ((void *(*)(void *, void *))rt_guest(CREATE_CONSTRUCT))(object, owner);
}

/* frees_block_at_0xa0_then_destroys_base_0x00569a90 (game/engine/block_at_0xa0_family.cpp): the
   block at +0xa0, its data, the free slot 0x68 of the allocator the global points at. */
#define BLOCK_GLOBAL_ALLOCATOR 0x057e7320u
#define BLOCK_FREE_SLOT 13
#define BLOCK_BASE_DESTROY 0x00569a90u

struct mutant_block_owner {
    uint8_t unknown_0x00[0xa0];
    void **block;
};
typedef void (*block_free)(void *, void *);

#define BLOCK_RIGHT 0
#define BLOCK_ONCE 1  /* frees the block read before the first free */
#define BLOCK_ALLOCATOR_ONCE 2  /* frees both through the allocator read before the first free */
#define BLOCK_SKIPS_DATA 4  /* frees only the block */

static void block_free_through(void **allocator, void *data)
{
    ((block_free)(*(void ***)allocator)[BLOCK_FREE_SLOT])(allocator, data);
}

static void block_destroy_with(struct mutant_block_owner *owner, int wrong)
{
    void **block = owner->block;
    if (block) {
        void **allocator = *(void **volatile *)rt_guest(BLOCK_GLOBAL_ALLOCATOR);
        if (!(wrong & BLOCK_SKIPS_DATA))
            block_free_through(allocator, block[0]);
        if (!(wrong & BLOCK_ALLOCATOR_ONCE))
            allocator = *(void **volatile *)rt_guest(BLOCK_GLOBAL_ALLOCATOR);
        block_free_through(allocator, (wrong & BLOCK_ONCE) ? block : *(void **volatile *)&owner->block);
    }
    ((void (*)(void *))rt_guest(BLOCK_BASE_DESTROY))(owner);
}

void bb_mutant_block_at_0xa0_once(struct mutant_block_owner *owner)
{
    block_destroy_with(owner, BLOCK_ONCE);
}

void bb_mutant_block_at_0xa0_allocator_once(struct mutant_block_owner *owner)
{
    block_destroy_with(owner, BLOCK_ALLOCATOR_ONCE);
}

void bb_mutant_block_at_0xa0_skips_data(struct mutant_block_owner *owner)
{
    block_destroy_with(owner, BLOCK_SKIPS_DATA);
}

/* destroys_owner_of_two_objects and deletes_owner_of_two_objects
   (game/engine/owner_of_two_family.cpp): the owner (vtable, first +0x8, second +0x10) and its
   objects, whose vtable slot 0x18 releases them. */
#define OWNER_VTABLE 0x0570f800u
#define OWNER_VTABLE_POINT 0x10u
#define OWNER_RELEASE_SLOT 3
#define OWNER_SIZED_FREE 0x02fa1c20u
#define OWNER_ALLOCATOR 0x05ac8288u
#define OWNER_SIZE 0x18u
#define OWNER_ALIGNMENT 8u

struct mutant_owned {
    void (**vtable)(struct mutant_owned *);
};
struct mutant_owner_of_two {
    const void *vtable;
    struct mutant_owned *first;
    struct mutant_owned *second;
};
typedef void (*owner_sized_free)(void *, void *, uint64_t, uint64_t);

#define OWNER_RIGHT 0
#define OWNER_SECOND_EARLY 1  /* reads the second object before the first release */
#define OWNER_FIRST_CLEARED_LATE 2  /* clears the first field only after both releases */
#define OWNER_NO_FINAL_CLEAR 4  /* does not clear the fields again at the end */
#define OWNER_SECOND_ALONE 8  /* releases the second object without a first one */
#define OWNER_ALLOCATOR_EARLY 16  /* reads the allocator before the releases */

static void owner_release(struct mutant_owned *object)
{
    object->vtable[OWNER_RELEASE_SLOT](object);
}

static void owner_destroy_with(struct mutant_owner_of_two *owner, int wrong)
{
    owner->vtable = (const uint8_t *)rt_guest(OWNER_VTABLE) + OWNER_VTABLE_POINT;
    struct mutant_owned *volatile *first = &owner->first;
    struct mutant_owned *volatile *second = &owner->second;
    if (!*first) {
        if ((wrong & OWNER_SECOND_ALONE) && *second)
            owner_release(*second);
        return;
    }
    struct mutant_owned *early = *second;
    owner_release(*first);
    if (!(wrong & OWNER_FIRST_CLEARED_LATE))
        *first = 0;
    struct mutant_owned *object = (wrong & OWNER_SECOND_EARLY) ? early : *second;
    if (object) {
        owner_release(object);
        *second = 0;
    }
    if (!(wrong & OWNER_NO_FINAL_CLEAR) || (wrong & OWNER_FIRST_CLEARED_LATE)) {
        *second = 0;
        *first = 0;
    }
}

static void owner_delete_with(struct mutant_owner_of_two *owner, int wrong)
{
    void *allocator = *(void *volatile *)rt_guest(OWNER_ALLOCATOR);
    owner_destroy_with(owner, wrong);
    if (!(wrong & OWNER_ALLOCATOR_EARLY))
        allocator = *(void *volatile *)rt_guest(OWNER_ALLOCATOR);
    ((owner_sized_free)rt_guest(OWNER_SIZED_FREE))(allocator, owner, OWNER_SIZE, OWNER_ALIGNMENT);
}

void bb_mutant_owner_of_two_second_early(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_SECOND_EARLY);
}

void bb_mutant_owner_of_two_first_cleared_late(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_FIRST_CLEARED_LATE);
}

void bb_mutant_owner_of_two_no_final_clear(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_NO_FINAL_CLEAR);
}

void bb_mutant_owner_of_two_second_alone(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_SECOND_ALONE);
}

void bb_mutant_owner_of_two_delete_second_early(struct mutant_owner_of_two *owner)
{
    owner_delete_with(owner, OWNER_SECOND_EARLY);
}

void bb_mutant_owner_of_two_delete_first_cleared_late(struct mutant_owner_of_two *owner)
{
    owner_delete_with(owner, OWNER_FIRST_CLEARED_LATE);
}

void bb_mutant_owner_of_two_delete_allocator_early(struct mutant_owner_of_two *owner)
{
    owner_delete_with(owner, OWNER_ALLOCATOR_EARLY);
}
