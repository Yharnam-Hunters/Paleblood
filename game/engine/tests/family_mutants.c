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

/* The class whose tables start at 0x0579c8a0 (the object's +0x10, a second +0x90); its base's
   tables at 0x0579a880 (+0x10, +0x78), its member's at 0x0579a0f0, the held blocks' at 0x0579a060.
   Offsets as vtable_0x0579c8a0_destructor_family.cpp names them. */
#define VT_OBJECT_TABLES 0x0579c8a0u
#define VT_BASE_TABLES 0x0579a880u
#define VT_MEMBER_TABLES 0x0579a0f0u
#define VT_HELD_TABLES 0x0579a060u
#define VT_TABLE_START 0x10u
#define VT_OBJECT_SECOND_TABLE 0x90u
#define VT_BASE_SECOND_TABLE 0x78u
#define VT_SECOND_VTABLE 0x48u
#define VT_BASE_MEMBER 0xa70u
#define VT_BASE_HELD 0xd30u
#define VT_FIRST_MEMBER 0xd90u
#define VT_MEMBER 0x1f20u
#define VT_MEMBER_HELD 0x1f30u
#define VT_TEXT 0x1fb0u
#define VT_CALLABLE_STORAGE 0x1ff0u
#define VT_CALLABLE 0x2010u
#define VT_HELD_AREA 0x30u
#define VT_HELD_RELEASER 0x40u
#define VT_HELD_FLAGS 0x48u
#define VT_HELD_VALUE 0x50u
#define VT_HELD_SET 0x40u
#define VT_TEXT_HEAP 0x8u
#define VT_TEXT_SIZE 0x18u
#define VT_TEXT_CAPACITY 0x20u
#define VT_TEXT_ALLOCATOR 0x28u
#define VT_TEXT_INLINE_CAPACITY 8u
#define VT_CALLABLE_DESTROY_SLOT 4
#define VT_FREE_SLOT 14
#define VT_RELEASE_SLOT 3
#define VT_DESTROY_FIRST_MEMBER 0x01f4cbb0u
#define VT_DESTROY_BASE_MEMBER 0x01ed24f0u
#define VT_DESTROY_BASE 0x01ed5390u
#define VT_OPERATOR_DELETE 0x02fbe588u

#define VT_FIELD(object, offset, type) (*(type *)((object) + (offset)))

typedef void (*vt_one_function)(void *);
typedef void (*vt_two_function)(void *, uint64_t);
typedef void (*vt_three_function)(void *, void *, uint64_t);

static void vt_call_one(uint32_t address, void *argument)
{
    ((vt_one_function)rt_guest(address))(argument);
}

/* What a held block's release reads: the flags and the releaser, on entry (snapshot) or when the
   code reaches it. */
struct vt_held_snapshot {
    uint32_t flags;
    void **releaser;
};

static void vt_release_held(uint8_t *held, uint32_t table, const struct vt_held_snapshot *snapshot)
{
    VT_FIELD(held, 0, uint64_t) = (uint64_t)((uint8_t *)rt_guest(table) + VT_TABLE_START);
    uint32_t flags = snapshot ? snapshot->flags : VT_FIELD(held, VT_HELD_FLAGS, uint32_t);
    void **releaser = snapshot ? snapshot->releaser : VT_FIELD(held, VT_HELD_RELEASER, void **);
    if (flags & VT_HELD_SET) {
        vt_three_function release = (vt_three_function)((void **)*releaser)[VT_RELEASE_SLOT];
        release(releaser, held + VT_HELD_AREA, VT_FIELD(held, VT_HELD_VALUE, uint64_t));
        VT_FIELD(held, VT_HELD_RELEASER, void *) = 0;
    }
    VT_FIELD(held, VT_HELD_FLAGS, uint32_t) = 0;
}

/* The destructor, with every field read where the code reads it unless `snapshot` (all of the
   string's capacity, the flags and the releasers read on entry), and the delete when `delete`. */
static void vt_destroy(uint8_t *object, int snapshot, int delete)
{
    uint8_t *text = object + VT_TEXT;
    uint64_t capacity = VT_FIELD(text, VT_TEXT_CAPACITY, uint64_t);
    struct vt_held_snapshot member_held = {VT_FIELD(object + VT_MEMBER_HELD, VT_HELD_FLAGS, uint32_t),
                                           VT_FIELD(object + VT_MEMBER_HELD, VT_HELD_RELEASER, void **)};
    struct vt_held_snapshot base_held = {VT_FIELD(object + VT_BASE_HELD, VT_HELD_FLAGS, uint32_t),
                                         VT_FIELD(object + VT_BASE_HELD, VT_HELD_RELEASER, void **)};
    uint8_t *tables = rt_guest(VT_OBJECT_TABLES);
    VT_FIELD(object, 0, uint64_t) = (uint64_t)(tables + VT_TABLE_START);
    VT_FIELD(object, VT_SECOND_VTABLE, uint64_t) = (uint64_t)(tables + VT_OBJECT_SECOND_TABLE);
    VT_FIELD(object, VT_MEMBER, uint64_t) = (uint64_t)((uint8_t *)rt_guest(VT_MEMBER_TABLES) + VT_TABLE_START);
    void **callable = VT_FIELD(object, VT_CALLABLE, void **);
    if (callable) {
        vt_two_function destroy = (vt_two_function)((void **)*callable)[VT_CALLABLE_DESTROY_SLOT];
        destroy(callable, (uint8_t *)callable != object + VT_CALLABLE_STORAGE);
        VT_FIELD(object, VT_CALLABLE, void *) = 0;
    }
    if (!snapshot)
        capacity = VT_FIELD(text, VT_TEXT_CAPACITY, uint64_t);
    if (capacity >= VT_TEXT_INLINE_CAPACITY) {
        void **allocator = VT_FIELD(text, VT_TEXT_ALLOCATOR, void **);
        vt_two_function release = (vt_two_function)((void **)*allocator)[VT_FREE_SLOT];
        release(allocator, VT_FIELD(text, VT_TEXT_HEAP, uint64_t));
    }
    VT_FIELD(text, VT_TEXT_CAPACITY, uint64_t) = VT_TEXT_INLINE_CAPACITY - 1;
    VT_FIELD(text, VT_TEXT_SIZE, uint64_t) = 0;
    VT_FIELD(text, VT_TEXT_HEAP, uint16_t) = 0;
    vt_release_held(object + VT_MEMBER_HELD, VT_HELD_TABLES, snapshot ? &member_held : 0);
    vt_call_one(VT_DESTROY_FIRST_MEMBER, object + VT_FIRST_MEMBER);
    tables = rt_guest(VT_BASE_TABLES);
    VT_FIELD(object, 0, uint64_t) = (uint64_t)(tables + VT_TABLE_START);
    VT_FIELD(object, VT_SECOND_VTABLE, uint64_t) = (uint64_t)(tables + VT_BASE_SECOND_TABLE);
    vt_release_held(object + VT_BASE_HELD, VT_HELD_TABLES, snapshot ? &base_held : 0);
    vt_call_one(VT_DESTROY_BASE_MEMBER, object + VT_BASE_MEMBER);
    vt_call_one(VT_DESTROY_BASE, object);
    if (delete)
        vt_call_one(VT_OPERATOR_DELETE, object);
}

/* Reads the string's capacity, both blocks' flags and both releasers on entry: wrong for
   stores_vtable_0x0579c8a0_releases_members_then_destroys, which reads each after the calls
   before it (the callable's destruction, the member at +0xd90's destructor). */
void bb_mutant_vtable_0x0579c8a0_destroy_reads_first(uint8_t *object)
{
    vt_destroy(object, 1, 0);
}

/* The same for the deleting twin. */
void bb_mutant_vtable_0x0579c8a0_delete_reads_first(uint8_t *object)
{
    vt_destroy(object, 1, 1);
}

/* Deletes the object too: wrong for stores_vtable_0x0579c8a0_releases_members_then_destroys. */
void bb_mutant_vtable_0x0579c8a0_destroy_then_delete(uint8_t *object)
{
    vt_destroy(object, 0, 1);
}

/* Does not delete the object: wrong for the deleting twin. */
void bb_mutant_vtable_0x0579c8a0_delete_without_delete(uint8_t *object)
{
    vt_destroy(object, 0, 0);
}

/* The quicksort of 16-byte elements by a comparator (sort_16_byte_elements_family.cpp), with
   three ways to get it wrong: the pivot by reference (no copy), the pivot taken at `first`, and the
   right part sorted by the call and the left by the loop. */
struct sort_element {
    uint8_t unknown_0x00[0x10];
};
typedef uint8_t (*sort_before)(const struct sort_element *, const struct sort_element *);
#define SORT_PIVOT_BY_REFERENCE 1
#define SORT_PIVOT_AT_FIRST 2
#define SORT_RECURSE_RIGHT 4

static void sort_elements(struct sort_element *elements, int32_t first, int32_t last, sort_before before, int how)
{
    for (;;) {
        int32_t middle = (int32_t)((uint32_t)first + (uint32_t)last) >> 1;
        if (how & SORT_PIVOT_AT_FIRST)
            middle = first;
        struct sort_element copy = elements[middle];
        const struct sort_element *pivot = (how & SORT_PIVOT_BY_REFERENCE) ? &elements[middle] : &copy;
        int32_t i = first, j = last;
        do {
            while (before(&elements[i], pivot))
                ++i;
            while (before(pivot, &elements[j]))
                --j;
            if (i <= j) {
                if (i != j) {
                    struct sort_element held = elements[j];
                    elements[j] = elements[i];
                    elements[i] = held;
                }
                ++i;
                --j;
            }
        } while (i <= j);
        if (how & SORT_RECURSE_RIGHT) {
            if (i < last)
                sort_elements(elements, i, last, before, how);
            if (first >= j)
                return;
            last = j;
            continue;
        }
        if (first < j)
            sort_elements(elements, first, j, before, how);
        if (i >= last)
            return;
        first = i;
    }
}

/* Compares with the middle element in place instead of a copy: wrong for
   sorts_16_byte_elements_with_comparator once a swap or a comparator moves or rewrites it. */
void bb_mutant_sort_pivot_by_reference(struct sort_element *elements, int32_t first, int32_t last, sort_before before)
{
    sort_elements(elements, first, last, before, SORT_PIVOT_BY_REFERENCE);
}

/* Takes the pivot at `first`: wrong for sorts_16_byte_elements_with_comparator (the middle). */
void bb_mutant_sort_pivot_at_first(struct sort_element *elements, int32_t first, int32_t last, sort_before before)
{
    sort_elements(elements, first, last, before, SORT_PIVOT_AT_FIRST);
}

/* Sorts the right part first: the same order in the end, but not the same calls. */
void bb_mutant_sort_recurses_right(struct sort_element *elements, int32_t first, int32_t last, sort_before before)
{
    sort_elements(elements, first, last, before, SORT_RECURSE_RIGHT);
}

/* The lookup of an id pair in nested hash tables (nested_hash_lookup_family.cpp): a table's end
   node (+0x8), buckets (+0x28, two 24-byte iterators each, the node at +0x10), mask (+0x48) and
   bucket count (+0x50); a node's next (+0x0), id (+0x10) and value (+0x18). The hash goes
   through the C library's ldiv (0x02fbfd68). Each mutant gets one thing wrong. */
#define HASH_LDIV 0x02fbfd68u
#define HASH_SEED 0xdeadbeefu
#define HASH_DIVISOR 127773
#define HASH_REMAINDER_FACTOR 16807u
#define HASH_QUOTIENT_FACTOR 2836u
#define HASH_WRAP 0x7fffffffffffffffu
#define HASH_OWNER_TABLE 0x8u
#define HASH_TABLE_END 0x8u
#define HASH_TABLE_BUCKETS 0x28u
#define HASH_TABLE_MASK 0x48u
#define HASH_TABLE_COUNT 0x50u
#define HASH_BUCKET_SIZE 0x30u
#define HASH_ITERATOR_NODE 0x10u
#define HASH_BUCKET_LAST 0x18u
#define HASH_NODE_ID 0x10u
#define HASH_NODE_VALUE 0x18u
#define HASH_SIGNED_IDS 1
#define HASH_NO_FOLD 2
#define HASH_FIRST_ONLY 4

#define HASH_FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

struct hash_quotient {
    int64_t quotient;
    int64_t remainder;
};
typedef struct hash_quotient (*hash_ldiv)(int64_t, int64_t);

static int hash_below(uint32_t a, uint32_t b, int how)
{
    return (how & HASH_SIGNED_IDS) ? (int32_t)a < (int32_t)b : a < b;
}

static uint8_t *hash_find(uint8_t *table, uint32_t id, int how)
{
    struct hash_quotient q = ((hash_ldiv)rt_guest(HASH_LDIV))((int64_t)(id ^ HASH_SEED), HASH_DIVISOR);
    uint64_t hash = (uint64_t)q.remainder * HASH_REMAINDER_FACTOR - (uint64_t)q.quotient * HASH_QUOTIENT_FACTOR;
    if ((int64_t)hash < 0)
        hash += HASH_WRAP;
    uint64_t mask = HASH_FIELD(table, HASH_TABLE_MASK, uint64_t);
    uint64_t index = hash & mask;
    if (!(how & HASH_NO_FOLD) && HASH_FIELD(table, HASH_TABLE_COUNT, uint64_t) <= index)
        index -= (mask >> 1) + 1;
    uint8_t *end = HASH_FIELD(table, HASH_TABLE_END, uint8_t *);
    uint8_t *bucket = HASH_FIELD(table, HASH_TABLE_BUCKETS, uint8_t *) + index * HASH_BUCKET_SIZE;
    uint8_t *node = HASH_FIELD(bucket, HASH_ITERATOR_NODE, uint8_t *);
    if (node == end)
        return end;
    uint8_t *stop = *(uint8_t **)HASH_FIELD(bucket, HASH_BUCKET_LAST + HASH_ITERATOR_NODE, uint8_t *);
    for (; node != stop; node = *(uint8_t **)node) {
        uint32_t key = HASH_FIELD(node, HASH_NODE_ID, uint32_t);
        if (!hash_below(key, id, how))
            return hash_below(id, key, how) ? end : node;
    }
    return end;
}

static uint8_t hash_pair(uint8_t *owner, const uint32_t *ids, int how)
{
    uint8_t *table = owner + HASH_OWNER_TABLE;
    uint8_t *node = hash_find(table, ids[0], how);
    if (node == HASH_FIELD(table, HASH_TABLE_END, uint8_t *))
        return 0;
    if (how & HASH_FIRST_ONLY)
        return 1;
    uint8_t *inner = HASH_FIELD(node, HASH_NODE_VALUE, uint8_t *);
    return hash_find(inner, ids[1], how) != HASH_FIELD(inner, HASH_TABLE_END, uint8_t *);
}

/* Orders ids as signed: wrong for finds_id_pair_in_nested_hash_maps (unsigned). */
uint8_t bb_mutant_hash_pair_signed_ids(uint8_t *owner, const uint32_t *ids)
{
    return hash_pair(owner, ids, HASH_SIGNED_IDS);
}

/* Does not fold an index past the bucket count: wrong once a hash lands there. */
uint8_t bb_mutant_hash_pair_without_fold(uint8_t *owner, const uint32_t *ids)
{
    return hash_pair(owner, ids, HASH_NO_FOLD);
}

/* Answers on the first id alone: wrong when the second is missing. */
uint8_t bb_mutant_hash_pair_first_only(uint8_t *owner, const uint32_t *ids)
{
    return hash_pair(owner, ids, HASH_FIRST_ONLY);
}
