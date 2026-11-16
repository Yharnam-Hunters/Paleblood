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

/* The kind-sized storage families (game/engine/kind_sized_storage.cpp): an object with a vtable
   whose slot 0x88 reports the kind, a buffer at +0x8, the elements at +0x10 and the count at
   +0x18. Each mutant below gets one detail wrong; the controls move one read across the kind
   calls. */
#define KIND_STORAGE_ALLOCATE 0x02fa1b80u
#define KIND_STORAGE_FREE 0x02fa1c20u
#define KIND_STORAGE_ALLOCATOR 0x05ac8288u
#define KIND_STORAGE_SLOT 17
#define KIND_THREE_PER_ENTRY 4u
#define KIND_THREE_PER_ENTRY_AND_TWO 6u
#define KIND_PER_ENTRY 3u
#define KIND_EXTRA 2u
#define KIND_ENTRY_SIZE 4u
#define KIND_DWORD_SIZE 4u
#define KIND_VEC4_SIZE 16u
#define KIND_VEC4_MASK 15u

struct kind_storage;
typedef int32_t (*kind_storage_kind)(struct kind_storage *, int32_t);
typedef void *(*kind_storage_allocate)(void *, uint64_t, uint64_t);
typedef void (*kind_storage_free)(void *, void *, uint64_t, uint64_t);

struct kind_storage {
    void **vtable;
    void *storage;
    void *elements;
    int32_t count;
};

/* What the mutant gets wrong. */
#define KIND_RIGHT 0
#define KIND_ALLOCATOR_LATE 1  /* reads the allocator after the kind calls */
#define KIND_COUNT_EARLY 2  /* reads the count before the kind calls */
#define KIND_ASKS_ONCE 4  /* uses the first answer for the kind 6 test too */
#define KIND_CACHES_VTABLE 8  /* calls the second time through the first vtable */
#define KIND_UNROUNDED 16  /* frees without rounding the entries up to 16 bytes */
#define KIND_NO_ENTRY_OFFSET 32  /* points the elements at the buffer itself */
#define KIND_TRUE_ON_FAILURE 64  /* reports success when the allocation fails */

static void *kind_storage_allocator(void)
{
    return *(void *volatile *)rt_guest(KIND_STORAGE_ALLOCATOR);
}

static uint32_t kind_storage_ask(struct kind_storage *object, void **vtable)
{
    return (uint32_t)((kind_storage_kind)vtable[KIND_STORAGE_SLOT])(object, 0) & ~1u;
}

/* The element count (32-bit arithmetic) and, in *COUNT, the count it was made from. */
static int32_t kind_storage_elements(struct kind_storage *object, int wrong, int32_t *count)
{
    volatile int32_t *field = &object->count;
    int32_t early = *field;
    void **vtable = object->vtable;
    uint32_t first = kind_storage_ask(object, vtable);
    if (first == KIND_THREE_PER_ENTRY) {
        *count = (wrong & KIND_COUNT_EARLY) ? early : *field;
        return (int32_t)((uint32_t)*count * KIND_PER_ENTRY);
    }
    uint32_t second = kind_storage_ask(object, (wrong & KIND_CACHES_VTABLE) ? vtable : object->vtable);
    if (wrong & KIND_ASKS_ONCE)
        second = first;
    *count = (wrong & KIND_COUNT_EARLY) ? early : *field;
    if (second == KIND_THREE_PER_ENTRY_AND_TWO)
        return (int32_t)((uint32_t)*count * KIND_PER_ENTRY + KIND_EXTRA);
    return *count;
}

static void kind_storage_free_with(struct kind_storage *object, int wrong)
{
    void *allocator = kind_storage_allocator();
    int32_t count;
    int32_t elements = kind_storage_elements(object, wrong, &count);
    if (wrong & KIND_ALLOCATOR_LATE)
        allocator = kind_storage_allocator();
    uint64_t entries = (uint64_t)(int64_t)count * KIND_ENTRY_SIZE;
    if (!(wrong & KIND_UNROUNDED))
        entries = (entries + KIND_VEC4_MASK) & ~(uint64_t)KIND_VEC4_MASK;
    ((kind_storage_free)rt_guest(KIND_STORAGE_FREE))(allocator, object->storage,
                                                     (uint64_t)(int64_t)elements * KIND_VEC4_SIZE + entries, KIND_VEC4_SIZE);
}

static uint8_t kind_storage_allocate_with(struct kind_storage *object, int wrong)
{
    void *allocator = kind_storage_allocator();
    int32_t count;
    int32_t elements = kind_storage_elements(object, wrong, &count);
    if (wrong & KIND_ALLOCATOR_LATE)
        allocator = kind_storage_allocator();
    uint64_t entries = (uint64_t)(int64_t)count * KIND_ENTRY_SIZE;
    uint8_t *storage = ((kind_storage_allocate)rt_guest(KIND_STORAGE_ALLOCATE))(
        allocator, (uint64_t)(int64_t)elements * KIND_DWORD_SIZE + entries, KIND_DWORD_SIZE);
    if (!storage)
        return (wrong & KIND_TRUE_ON_FAILURE) ? 1 : 0;
    object->storage = storage;
    object->elements = (wrong & KIND_NO_ENTRY_OFFSET) ? storage : storage + entries;
    return 1;
}

void bb_mutant_kind_storage_free_reads_allocator_late(struct kind_storage *object)
{
    kind_storage_free_with(object, KIND_ALLOCATOR_LATE);
}

void bb_mutant_kind_storage_free_reads_count_early(struct kind_storage *object)
{
    kind_storage_free_with(object, KIND_COUNT_EARLY);
}

void bb_mutant_kind_storage_free_asks_once(struct kind_storage *object)
{
    kind_storage_free_with(object, KIND_ASKS_ONCE);
}

void bb_mutant_kind_storage_free_caches_vtable(struct kind_storage *object)
{
    kind_storage_free_with(object, KIND_CACHES_VTABLE);
}

void bb_mutant_kind_storage_free_unrounded(struct kind_storage *object)
{
    kind_storage_free_with(object, KIND_UNROUNDED);
}

uint8_t bb_mutant_kind_storage_allocate_reads_allocator_late(struct kind_storage *object)
{
    return kind_storage_allocate_with(object, KIND_ALLOCATOR_LATE);
}

uint8_t bb_mutant_kind_storage_allocate_reads_count_early(struct kind_storage *object)
{
    return kind_storage_allocate_with(object, KIND_COUNT_EARLY);
}

uint8_t bb_mutant_kind_storage_allocate_asks_once(struct kind_storage *object)
{
    return kind_storage_allocate_with(object, KIND_ASKS_ONCE);
}

uint8_t bb_mutant_kind_storage_allocate_caches_vtable(struct kind_storage *object)
{
    return kind_storage_allocate_with(object, KIND_CACHES_VTABLE);
}

uint8_t bb_mutant_kind_storage_allocate_no_entry_offset(struct kind_storage *object)
{
    return kind_storage_allocate_with(object, KIND_NO_ENTRY_OFFSET);
}

uint8_t bb_mutant_kind_storage_allocate_true_on_failure(struct kind_storage *object)
{
    return kind_storage_allocate_with(object, KIND_TRUE_ON_FAILURE);
}

/* The setter twin: leaves the count alone, or stores it only after asking the kind. */
uint8_t bb_mutant_kind_storage_set_count_skipped(struct kind_storage *object, int32_t count)
{
    (void)count;
    return kind_storage_allocate_with(object, KIND_RIGHT);
}

uint8_t bb_mutant_kind_storage_set_count_after_kind(struct kind_storage *object, int32_t count)
{
    void *allocator = kind_storage_allocator();
    uint32_t kind = kind_storage_ask(object, object->vtable);
    if (kind != KIND_THREE_PER_ENTRY)
        kind = kind_storage_ask(object, object->vtable);
    object->count = count;
    int32_t elements = kind == KIND_THREE_PER_ENTRY ? (int32_t)((uint32_t)count * KIND_PER_ENTRY)
                     : kind == KIND_THREE_PER_ENTRY_AND_TWO ? (int32_t)((uint32_t)count * KIND_PER_ENTRY + KIND_EXTRA) : count;
    uint64_t entries = (uint64_t)(int64_t)count * KIND_ENTRY_SIZE;
    uint8_t *storage = ((kind_storage_allocate)rt_guest(KIND_STORAGE_ALLOCATE))(
        allocator, (uint64_t)(int64_t)elements * KIND_DWORD_SIZE + entries, KIND_DWORD_SIZE);
    if (!storage)
        return 0;
    object->storage = storage;
    object->elements = storage + entries;
    return 1;
}

