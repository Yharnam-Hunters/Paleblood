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


/* destroys_holder_of_shared_work (game/engine/shared_work_holder_family.cpp): a holder with a
   vtable and its work at +0x8; the work's references at +0x8, signal at +0x10, users at +0x30.
   Each mutant below gets one detail wrong. */
#define HOLDER_BASE_VTABLE 0x057b1a80u
#define HOLDER_SIGNAL_SET 0x0247e780u
#define HOLDER_FATAL_ERROR 0x024b55b0u
#define HOLDER_INVALID_UNREF 0x04d40331u
#define HOLDER_INVALID_UNREF_LINE 0x3e
#define HOLDER_SIGNAL_OFFSET 0x10
#define HOLDER_LAST_USER 1

struct shared_work {
    void (**vtable)(struct shared_work *);
    int32_t references;
    uint8_t unknown_0x0c[0x24];
    int32_t users;
};

struct work_holder {
    void *vtable;
    struct shared_work *work;
};

typedef uint64_t (*holder_signal_set)(void *, _Bool);
typedef void (*holder_fatal_error)(const char *, int32_t, const char *, ...);

/* What the mutant gets wrong. */
#define HOLDER_WORK_ONCE 1  /* does not read the work again after the signal */
#define HOLDER_SIGNAL_ON_ONE 2  /* sets the signal only at exactly one user */
#define HOLDER_KEEPS_WORK 4  /* leaves the work pointer set */

static void work_holder_destroy_with(struct work_holder *holder, int wrong)
{
    holder->vtable = rt_guest(HOLDER_BASE_VTABLE);
    struct shared_work *work = holder->work;
    if (!work)
        return;
    int32_t users = __atomic_fetch_sub(&work->users, 1, __ATOMIC_SEQ_CST);
    if ((wrong & HOLDER_SIGNAL_ON_ONE) ? users == HOLDER_LAST_USER : users <= HOLDER_LAST_USER)
        ((holder_signal_set)rt_guest(HOLDER_SIGNAL_SET))((uint8_t *)work + HOLDER_SIGNAL_OFFSET, 1);
    if (!(wrong & HOLDER_WORK_ONCE))
        work = holder->work;
    if (!work)
        return;
    int32_t references = __atomic_fetch_sub(&work->references, 1, __ATOMIC_SEQ_CST);
    if (references == 1)
        work->vtable[0](work);
    else if (references <= 0)
        ((holder_fatal_error)rt_guest(HOLDER_FATAL_ERROR))(0, HOLDER_INVALID_UNREF_LINE,
                                                          (const char *)rt_guest(HOLDER_INVALID_UNREF));
    if (!(wrong & HOLDER_KEEPS_WORK))
        holder->work = 0;
}

void bb_mutant_holder_of_shared_work_work_once(struct work_holder *holder)
{
    work_holder_destroy_with(holder, HOLDER_WORK_ONCE);
}

void bb_mutant_holder_of_shared_work_signal_on_one(struct work_holder *holder)
{
    work_holder_destroy_with(holder, HOLDER_SIGNAL_ON_ONE);
}

void bb_mutant_holder_of_shared_work_keeps_work(struct work_holder *holder)
{
    work_holder_destroy_with(holder, HOLDER_KEEPS_WORK);
}

/* destroys_owner_of_wide_string_then_base_0x0139b630 and
   deletes_owner_of_wide_string_after_base_0x0139b630 (game/engine/wide_string_owner_family.cpp):
   an owner with a vtable, a wide string object at +0x30 and its owned flag at +0x44; the string's
   text at +0x8, size +0x18, capacity +0x20 and allocator +0x28. */
#define STRING_OWNER_VTABLE 0x056f3b40u
#define STRING_HEAP_OWNING 0x0247b720u
#define STRING_BASE_DESTROY 0x0139b630u
#define STRING_OPERATOR_DELETE 0x02fbe588u
#define STRING_FREE_SLOT 14
#define STRING_INLINE_CAPACITY 8u
#define STRING_EMPTY_CAPACITY 7u

struct string_allocator {
    void (**vtable)(struct string_allocator *, void *);
};

struct owned_wide_string {
    uint64_t unknown_0x00;
    union {
        uint16_t inline_text[STRING_INLINE_CAPACITY];
        void *heap;
    };
    uint64_t size;
    uint64_t capacity;
    struct string_allocator *allocator;
};

struct wide_string_owner {
    void *vtable;
    uint8_t unknown_0x08[0x28];
    struct owned_wide_string *string;
    uint8_t unknown_0x38[0xc];
    uint8_t owns_string;
};

typedef struct string_allocator *(*string_heap_owning)(void *);
typedef void (*string_owner_call)(void *);

/* What the mutant gets wrong. */
#define STRING_IGNORES_FLAG 1  /* releases the string without the owned flag */
#define STRING_CAPACITY_EARLY 2  /* reads the capacity before the heap lookup */
#define STRING_ALLOCATOR_EARLY 4  /* reads the string's allocator before the heap lookup */
#define STRING_FREES_INLINE 8  /* frees the text at capacity 7 too */
#define STRING_KEEPS_POINTER 16  /* leaves the string pointer set */
#define STRING_NO_RESET 32  /* frees the string object without emptying it */
#define STRING_DELETE_FIRST 64  /* deletes the owner before the base destructor */
#define STRING_KEPT 128  /* does not delete the owner */

static void string_owner_destroy_with(struct wide_string_owner *owner, int wrong, int deleting)
{
    owner->vtable = rt_guest(STRING_OWNER_VTABLE);
    struct owned_wide_string *string = owner->string;
    if ((owner->owns_string || (wrong & STRING_IGNORES_FLAG)) && string) {
        uint64_t early_capacity = string->capacity;
        struct string_allocator *early_allocator = string->allocator;
        struct string_allocator *heap = ((string_heap_owning)rt_guest(STRING_HEAP_OWNING))(string);
        uint64_t capacity = (wrong & STRING_CAPACITY_EARLY) ? early_capacity : string->capacity;
        struct string_allocator *allocator = (wrong & STRING_ALLOCATOR_EARLY) ? early_allocator : string->allocator;
        if (capacity >= ((wrong & STRING_FREES_INLINE) ? STRING_EMPTY_CAPACITY : STRING_INLINE_CAPACITY))
            allocator->vtable[STRING_FREE_SLOT](allocator, string->heap);
        if (!(wrong & STRING_NO_RESET)) {
            string->capacity = STRING_EMPTY_CAPACITY;
            string->size = 0;
            string->inline_text[0] = 0;
        }
        heap->vtable[STRING_FREE_SLOT](heap, string);
        if (!(wrong & STRING_KEEPS_POINTER))
            owner->string = 0;
    }
    if (deleting && (wrong & STRING_DELETE_FIRST))
        ((string_owner_call)rt_guest(STRING_OPERATOR_DELETE))(owner);
    ((string_owner_call)rt_guest(STRING_BASE_DESTROY))(owner);
    if (deleting && !(wrong & (STRING_DELETE_FIRST | STRING_KEPT)))
        ((string_owner_call)rt_guest(STRING_OPERATOR_DELETE))(owner);
}

void bb_mutant_string_owner_ignores_flag(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_IGNORES_FLAG, 0);
}

void bb_mutant_string_owner_capacity_early(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_CAPACITY_EARLY, 0);
}

void bb_mutant_string_owner_allocator_early(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_ALLOCATOR_EARLY, 0);
}

void bb_mutant_string_owner_frees_inline(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_FREES_INLINE, 0);
}

void bb_mutant_string_owner_keeps_pointer(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_KEEPS_POINTER, 0);
}

void bb_mutant_string_owner_no_reset(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_NO_RESET, 0);
}

void bb_mutant_string_owner_delete_capacity_early(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_CAPACITY_EARLY, 1);
}

void bb_mutant_string_owner_delete_first(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_DELETE_FIRST, 1);
}

void bb_mutant_string_owner_delete_kept(struct wide_string_owner *owner)
{
    string_owner_destroy_with(owner, STRING_KEPT, 1);
}

/* visits_children_of_list_at_0x40 and secondary_base_visits_children_of_list_at_0x40
   (game/engine/child_list_visit_family.cpp): an object with a vtable (slot 0x28 takes a child)
   and a list at +0x40 holding the children array at +0xa0 and the count at +0xa8. */
#define VISIT_PASS 0x00b69e20u
#define VISIT_SLOT 5
#define VISIT_PART_OFFSET 0x10
#define VISIT_SECONDARY_OFFSET 0x48

struct child_list {
    uint8_t unknown_0x00[0xa0];
    void **children;
    int32_t count;
};

struct child_visitor {
    void (**vtable)(struct child_visitor *, void *);
    uint8_t unknown_0x08[0x38];
    struct child_list *list;
};

typedef void (*visit_pass)(void *, void *);

/* What the mutant gets wrong. */
#define VISIT_RELOADS_COUNT 1  /* reads the count again for every child */
#define VISIT_CACHES_LIST 2  /* reads the list once */
#define VISIT_CACHES_ARRAY 4  /* reads the children array once */
#define VISIT_SLOT_FIRST 8  /* calls the slot before 0x00b69e20 */

static void visit_children_with(struct child_visitor *visitor, int wrong)
{
    struct child_list *list = visitor->list;
    if (!list)
        return;
    int32_t count = list->count;
    void **array = list->children;
    void *part = (uint8_t *)visitor + VISIT_PART_OFFSET;
    for (int32_t index = 0; index < ((wrong & VISIT_RELOADS_COUNT) ? visitor->list->count : count); ++index) {
        struct child_list *now = (wrong & VISIT_CACHES_LIST) ? list : visitor->list;
        void *child = ((wrong & VISIT_CACHES_ARRAY) ? array : now->children)[index];
        if (wrong & VISIT_SLOT_FIRST)
            visitor->vtable[VISIT_SLOT](visitor, child);
        ((visit_pass)rt_guest(VISIT_PASS))(child, part);
        if (!(wrong & VISIT_SLOT_FIRST))
            visitor->vtable[VISIT_SLOT](visitor, child);
    }
}

static struct child_visitor *visitor_of_secondary_base(uint8_t *secondary_base)
{
    return (struct child_visitor *)(secondary_base - VISIT_SECONDARY_OFFSET);
}

void bb_mutant_visit_children_reloads_count(struct child_visitor *visitor)
{
    visit_children_with(visitor, VISIT_RELOADS_COUNT);
}

void bb_mutant_visit_children_caches_list(struct child_visitor *visitor)
{
    visit_children_with(visitor, VISIT_CACHES_LIST);
}

void bb_mutant_visit_children_caches_array(struct child_visitor *visitor)
{
    visit_children_with(visitor, VISIT_CACHES_ARRAY);
}

void bb_mutant_visit_children_slot_first(struct child_visitor *visitor)
{
    visit_children_with(visitor, VISIT_SLOT_FIRST);
}

void bb_mutant_secondary_visit_children_reloads_count(uint8_t *secondary_base)
{
    visit_children_with(visitor_of_secondary_base(secondary_base), VISIT_RELOADS_COUNT);
}

void bb_mutant_secondary_visit_children_caches_list(uint8_t *secondary_base)
{
    visit_children_with(visitor_of_secondary_base(secondary_base), VISIT_CACHES_LIST);
}

void bb_mutant_secondary_visit_children_slot_first(uint8_t *secondary_base)
{
    visit_children_with(visitor_of_secondary_base(secondary_base), VISIT_SLOT_FIRST);
}

/* selects_index_of_changed_id_with_0x01348fe0 (game/engine/changed_id_index_family.cpp): an
   object with its owner at +0x18 (target at +0x110), the seen id at +0x20, the followed id's
   pointer at +0x28 and the table at +0x30 (24-byte entries from +0x18 to +0x20, id at +0x10). */
#define SELECT_APPLY 0x01348fe0u
#define SELECT_NO_INDEX 0xffffffffu

struct id_entry {
    uint8_t unknown_0x00[0x10];
    int32_t id;
    uint8_t unknown_0x14[0x4];
};

struct id_table {
    uint8_t unknown_0x00[0x18];
    struct id_entry *begin;
    struct id_entry *end;
};

struct selection_owner {
    uint8_t unknown_0x000[0x110];
    void *target;
};

struct id_follower {
    uint8_t unknown_0x00[0x18];
    struct selection_owner *owner;
    int32_t seen_id;
    uint8_t unknown_0x24[0x4];
    int32_t *followed_id;
    struct id_table *table;
};

typedef void (*select_apply)(void *, uint32_t, uint32_t);

/* What the mutant gets wrong. */
#define SELECT_KEEPS_SEEN 1  /* does not store the new id */
#define SELECT_FINDS_LAST 2  /* takes the last matching entry */
#define SELECT_ABSENT_AS_ZERO 4  /* passes index 0 when no entry matches */
#define SELECT_UNCHANGED_TOO 8  /* goes on when the id has not changed */

static void select_changed_id_with(struct id_follower *follower, int wrong)
{
    int32_t *followed = follower->followed_id;
    if (!followed)
        return;
    int32_t id = *followed;
    if (id == follower->seen_id && !(wrong & SELECT_UNCHANGED_TOO))
        return;
    if (!(wrong & SELECT_KEEPS_SEEN))
        follower->seen_id = id;
    struct id_table *table = follower->table;
    uint32_t count = (uint32_t)(table->end - table->begin);
    uint32_t index = (wrong & SELECT_ABSENT_AS_ZERO) ? 0 : SELECT_NO_INDEX;
    for (uint32_t i = 0; i < count; ++i) {
        if (table->begin[i].id == id) {
            index = i;
            if (!(wrong & SELECT_FINDS_LAST))
                break;
        }
    }
    ((select_apply)rt_guest(SELECT_APPLY))(follower->owner->target, index, 0);
}

void bb_mutant_select_changed_id_keeps_seen(struct id_follower *follower)
{
    select_changed_id_with(follower, SELECT_KEEPS_SEEN);
}

void bb_mutant_select_changed_id_finds_last(struct id_follower *follower)
{
    select_changed_id_with(follower, SELECT_FINDS_LAST);
}

void bb_mutant_select_changed_id_absent_as_zero(struct id_follower *follower)
{
    select_changed_id_with(follower, SELECT_ABSENT_AS_ZERO);
}

void bb_mutant_select_changed_id_unchanged_too(struct id_follower *follower)
{
    select_changed_id_with(follower, SELECT_UNCHANGED_TOO);
}
