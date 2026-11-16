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

/* The three owner block families of game/engine/block_init_and_owned_release_families.cpp: fixed
   addresses in the block's first qwords, the rest of qwords 0-14 cleared, the owner at +0x78, 0
   returned. */
#define MUTANT_INIT_BLOCK_SIX 0x05ac5cacu
#define MUTANT_INIT_BLOCK_FIRST 0x05acd45cu
#define MUTANT_INIT_BLOCK_SECOND 0x05acd1bcu
#define MUTANT_INIT_BLOCK_ONE 0x05ac74a3u
#define MUTANT_INIT_BLOCK_QWORDS 15
#define MUTANT_INIT_BLOCK_FIVE 5
#define MUTANT_INIT_BLOCK_PAIR 2
#define MUTANT_VTABLE_ADDRESS_POINT 0x10
struct mutant_init_block {
    const void *qwords[MUTANT_INIT_BLOCK_QWORDS];
    void *owner;
};

static int32_t mutant_init_block(void *owner, struct mutant_init_block *block, uint32_t first, uint32_t second,
                                 int pointers, int cleared)
{
    for (int i = 0; i < cleared; ++i)
        block->qwords[i] = i == 0 ? rt_guest(first) : i < pointers ? rt_guest(second) : 0;
    block->owner = owner;
    return 0;
}

/* Stores 0x05ac5cac in qwords 0-4 only (qword 5 cleared): wrong for
   initializes_owner_block_six_pointers_to_0x05ac5cac. */
int32_t bb_mutant_init_block_five_pointers(void *owner, struct mutant_init_block *block)
{
    return mutant_init_block(owner, block, MUTANT_INIT_BLOCK_SIX, MUTANT_INIT_BLOCK_SIX, MUTANT_INIT_BLOCK_FIVE,
                             MUTANT_INIT_BLOCK_QWORDS);
}

/* Stores the two addresses the other way round: wrong for
   initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc. */
int32_t bb_mutant_init_block_pair_swapped(void *owner, struct mutant_init_block *block)
{
    return mutant_init_block(owner, block, MUTANT_INIT_BLOCK_SECOND, MUTANT_INIT_BLOCK_FIRST, MUTANT_INIT_BLOCK_PAIR,
                             MUTANT_INIT_BLOCK_QWORDS);
}

/* Leaves the last cleared qword (+0x70) alone: wrong for
   initializes_owner_block_first_pointer_to_0x05ac74a3. */
int32_t bb_mutant_init_block_one_keeps_0x70(void *owner, struct mutant_init_block *block)
{
    return mutant_init_block(owner, block, MUTANT_INIT_BLOCK_ONE, 0, 1, MUTANT_INIT_BLOCK_QWORDS - 1);
}

/* Fills the block as initializes_owner_block_first_pointer_to_0x05ac74a3 does and returns 1:
   wrong for all three block families, which return 0. */
int32_t bb_mutant_init_block_returns_one(void *owner, struct mutant_init_block *block)
{
    mutant_init_block(owner, block, MUTANT_INIT_BLOCK_ONE, 0, 1, MUTANT_INIT_BLOCK_QWORDS);
    return 1;
}

/* destroys_pair_owner_with_vtable_0x0570fb90: the vtable 0x0570fb90 + 0x10, the object at +0x8
   (then the one at +0x10) released through slot 0x18 and both fields cleared. */
#define MUTANT_PAIR_OWNER_VTABLE 0x0570fb90u
struct mutant_pair_object;
struct mutant_pair_object_vtable {
    void *unknown_slots[3];
    void (*release)(struct mutant_pair_object *);
};
struct mutant_pair_object {
    const struct mutant_pair_object_vtable *vtable;
};
struct mutant_pair_owner {
    const uint8_t *vtable;
    struct mutant_pair_object *first;
    struct mutant_pair_object *second;
};

static void mutant_pair_release(struct mutant_pair_object *object)
{
    object->vtable->release(object);
}

static void mutant_pair_destroy(struct mutant_pair_owner *owner, int second_early, int first_late, int final_clear,
                                int second_alone)
{
    owner->vtable = (const uint8_t *)rt_guest(MUTANT_PAIR_OWNER_VTABLE) + MUTANT_VTABLE_ADDRESS_POINT;
    struct mutant_pair_object *second = owner->second;
    if (!owner->first) {
        if (second_alone && second) {
            mutant_pair_release(second);
            owner->second = 0;
        }
        return;
    }
    mutant_pair_release(owner->first);
    if (!first_late)
        owner->first = 0;
    if (!second_early)
        second = owner->second;
    if (second) {
        mutant_pair_release(second);
        owner->second = 0;
    }
    if (first_late)
        owner->first = 0;
    if (final_clear) {
        owner->second = 0;
        owner->first = 0;
    }
}

/* Reads the second object before the first release: wrong for
   destroys_pair_owner_with_vtable_0x0570fb90, which reads it after. */
void bb_mutant_pair_owner_second_early(struct mutant_pair_owner *owner)
{
    mutant_pair_destroy(owner, 1, 0, 1, 0);
}

/* Clears the first field only after the second release: wrong for
   destroys_pair_owner_with_vtable_0x0570fb90, which clears it before. */
void bb_mutant_pair_owner_first_cleared_late(struct mutant_pair_owner *owner)
{
    mutant_pair_destroy(owner, 0, 1, 1, 0);
}

/* Leaves out the final clear of both fields: wrong for
   destroys_pair_owner_with_vtable_0x0570fb90. */
void bb_mutant_pair_owner_no_final_clear(struct mutant_pair_owner *owner)
{
    mutant_pair_destroy(owner, 0, 0, 0, 0);
}

/* Releases the second object without a first one: wrong for
   destroys_pair_owner_with_vtable_0x0570fb90, which then leaves it alone. */
void bb_mutant_pair_owner_second_alone(struct mutant_pair_owner *owner)
{
    mutant_pair_destroy(owner, 0, 0, 1, 1);
}

/* destroys_entry_storage_with_vtable_0x057045e0: the vtable 0x057045e0 + 0x10, +0x20 cleared, the
   storage at +0x18 freed (count at +0x24 times 16 bytes, 32 bits) through slot 0x28 of the
   allocator object 0x058018b0 unless the count is negative, then +0x18 cleared and the count
   0x80000000. */
#define MUTANT_ENTRY_STORAGE_VTABLE 0x057045e0u
#define MUTANT_ENTRY_STORAGE_ALLOCATOR 0x058018b0u
#define MUTANT_ENTRY_STORAGE_ENTRY_BYTES 16
#define MUTANT_ENTRY_STORAGE_SHIFT 4
#define MUTANT_ENTRY_STORAGE_RELEASED ((int32_t)0x80000000u)
struct mutant_storage_allocator;
struct mutant_storage_allocator_vtable {
    uint8_t unknown_0x00[0x28];
    void (*free)(struct mutant_storage_allocator *, void *storage, uint64_t bytes);
};
struct mutant_storage_allocator {
    const struct mutant_storage_allocator_vtable *vtable;
};
struct mutant_entry_storage_owner {
    const uint8_t *vtable;
    uint8_t unknown_0x08[0x10];
    void *storage;
    uint32_t value_0x20;
    int32_t count;
};

static void mutant_entry_storage(struct mutant_entry_storage_owner *owner, int skip_zero, int wide, int clear_late,
                                 int set_count)
{
    owner->vtable = (const uint8_t *)rt_guest(MUTANT_ENTRY_STORAGE_VTABLE) + MUTANT_VTABLE_ADDRESS_POINT;
    if (!clear_late)
        owner->value_0x20 = 0;
    if (owner->count > 0 || (owner->count == 0 && !skip_zero)) {
        uint64_t bytes = wide ? (uint64_t)(int64_t)owner->count * MUTANT_ENTRY_STORAGE_ENTRY_BYTES
                              : (uint32_t)owner->count << MUTANT_ENTRY_STORAGE_SHIFT;
        struct mutant_storage_allocator *allocator = rt_guest(MUTANT_ENTRY_STORAGE_ALLOCATOR);
        allocator->vtable->free(allocator, owner->storage, bytes);
    }
    if (clear_late)
        owner->value_0x20 = 0;
    owner->storage = 0;
    if (set_count)
        owner->count = MUTANT_ENTRY_STORAGE_RELEASED;
}

/* Frees nothing for a count of 0: wrong for destroys_entry_storage_with_vtable_0x057045e0, which
   frees with 0 bytes. */
void bb_mutant_entry_storage_skips_zero_count(struct mutant_entry_storage_owner *owner)
{
    mutant_entry_storage(owner, 1, 0, 0, 1);
}

/* Passes the byte count in 64 bits: wrong for destroys_entry_storage_with_vtable_0x057045e0, whose
   32-bit count wraps. */
void bb_mutant_entry_storage_wide_bytes(struct mutant_entry_storage_owner *owner)
{
    mutant_entry_storage(owner, 0, 1, 0, 1);
}

/* Clears +0x20 after the free: wrong for destroys_entry_storage_with_vtable_0x057045e0, which
   clears it before. */
void bb_mutant_entry_storage_clears_0x20_late(struct mutant_entry_storage_owner *owner)
{
    mutant_entry_storage(owner, 0, 0, 1, 1);
}

/* Keeps the count: wrong for destroys_entry_storage_with_vtable_0x057045e0, which sets it to
   0x80000000. */
void bb_mutant_entry_storage_keeps_count(struct mutant_entry_storage_owner *owner)
{
    mutant_entry_storage(owner, 0, 0, 0, 0);
}

/* releases_counted_object_at_0x10: the object at +0x10 passed to 0x027286b0, the field read
   again, the object's counts at +0x8 (strong bits 0-14, weak bits 15-29, alive bit 30; destroy
   slot 0x18, free slot 0x20) dropped and the field cleared. */
#define MUTANT_COUNTED_BEFORE 0x027286b0u
#define MUTANT_COUNTED_MASK 0x7fffu
#define MUTANT_COUNTED_WEAK_SHIFT 15
#define MUTANT_COUNTED_ALIVE 0x40000000u
#define MUTANT_COUNTED_KEEP_WITH_WEAK 0xc0007fffu
struct mutant_counted_0x10;
typedef void mutant_counted_0x10_fn(struct mutant_counted_0x10 *counted);
struct mutant_counted_0x10_vtable {
    void *unknown_slots[3];
    mutant_counted_0x10_fn *destroy;
    mutant_counted_0x10_fn *free;
};
struct mutant_counted_0x10 {
    const struct mutant_counted_0x10_vtable *vtable;
    uint32_t counts;
};
struct mutant_counted_0x10_owner {
    uint8_t unknown_0x00[0x10];
    struct mutant_counted_0x10 *counted;
};

static void mutant_counted_0x10(struct mutant_counted_0x10_owner *owner, int field_again, int counts_again,
                                int weak_marked, int clear_field)
{
    struct mutant_counted_0x10 *counted = owner->counted;
    if (!counted)
        return;
    ((mutant_counted_0x10_fn *)rt_guest(MUTANT_COUNTED_BEFORE))(counted);
    if (field_again)
        counted = owner->counted;
    if (!counted)
        return;
    const uint32_t old = counted->counts;
    uint32_t counts = (old & ~MUTANT_COUNTED_MASK) | ((old - 1) & MUTANT_COUNTED_MASK);
    counted->counts = counts;
    if (!(counts & MUTANT_COUNTED_MASK) && (old & MUTANT_COUNTED_ALIVE)) {
        counted->vtable->destroy(counted);
        counts = (counts_again ? counted->counts : counts) & ~MUTANT_COUNTED_ALIVE;
        counted->counts = counts;
    }
    const uint32_t weak = ((counts >> MUTANT_COUNTED_WEAK_SHIFT) - 1) & MUTANT_COUNTED_MASK;
    counted->counts = (counts & MUTANT_COUNTED_KEEP_WITH_WEAK) | (weak << MUTANT_COUNTED_WEAK_SHIFT);
    if (!weak) {
        if (weak_marked)
            counted->counts = counts | (MUTANT_COUNTED_MASK << MUTANT_COUNTED_WEAK_SHIFT);
        counted->vtable->free(counted);
    }
    if (clear_field)
        owner->counted = 0;
}

/* Keeps the object it passed to 0x027286b0: wrong for releases_counted_object_at_0x10, which reads
   the field again after that call. */
void bb_mutant_counted_0x10_field_once(struct mutant_counted_0x10_owner *owner)
{
    mutant_counted_0x10(owner, 0, 1, 1, 1);
}

/* Keeps the counts from before the destroy: wrong for releases_counted_object_at_0x10, which
   reads them again. */
void bb_mutant_counted_0x10_counts_once(struct mutant_counted_0x10_owner *owner)
{
    mutant_counted_0x10(owner, 1, 0, 1, 1);
}

/* Leaves the weak count at zero when freeing: wrong for releases_counted_object_at_0x10, which
   sets every weak bit. */
void bb_mutant_counted_0x10_weak_zero(struct mutant_counted_0x10_owner *owner)
{
    mutant_counted_0x10(owner, 1, 1, 0, 1);
}

/* Keeps the pointer at +0x10: wrong for releases_counted_object_at_0x10. */
void bb_mutant_counted_0x10_keeps_pointer(struct mutant_counted_0x10_owner *owner)
{
    mutant_counted_0x10(owner, 1, 1, 1, 0);
}
