/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Control mutants for the scaleform dispatch and visit families
   (game/engine/scaleform_visit_dispatch.cpp, game/engine/family_cases.py MUTANTS): each is the
   right body with one read moved to the other side of a call or a store, so the cases that make
   the call or the store change that value must catch it on every member. */
#include <stddef.h>
#include <stdint.h>

#include "runtime/guest.h"

/* --- dispatches_on_slot_match_with_member_* ------------------------------------------------ */

struct dispatch_handle {
    uint8_t unknown_0x0[0x10];
    const void *target;
};

struct dispatch_slots {
    uint8_t unknown_0x0[0x3b0];
    struct dispatch_handle *second;
    struct dispatch_handle *first;
};

struct dispatch_context {
    uint8_t unknown_0x0[0x18];
    struct dispatch_slots *slots;
};

struct dispatch_owner {
    uint8_t unknown_0x0[0x28];
    struct dispatch_context *context;
};

struct dispatch_value {
    struct dispatch_owner *owner;
    uint8_t kind;
};

#define FIRST_POINTER_COMPARED_KIND 7u
#define RESOLVE_HANDLE_TARGET 0x004f0330u
#define DISPATCH_WITHOUT_SLOT 0x0056d2c0u
#define DISPATCH_WITH_SLOT 0x005c9830u

typedef void resolve_fn(struct dispatch_handle *handle);
typedef _Bool without_slot_fn(void *self, struct dispatch_value *value, struct dispatch_handle **handle,
                              void *argument);
typedef _Bool with_slot_fn(void *member, void *self, struct dispatch_value *value,
                           struct dispatch_handle **handle, void *argument, _Bool first_slot);

/* Which read each mutant moves. */
enum dispatch_flaw {
    FIRST_SLOT_BEFORE_RESOLVE,   /* read the first slot before resolving the argument */
    HANDLE_NOT_RELOADED,         /* keep the handle read before resolving */
    SLOTS_AFTER_RESOLVE,         /* read the owner chain after resolving, for the first slot */
    SLOTS_KEPT_FOR_SECOND,       /* use the first slot's owner chain for the second slot */
    KIND_KEPT_FOR_SECOND,        /* use the first slot's kind for the second slot */
    SECOND_SLOT_BEFORE_RESOLVE,  /* read the second slot before resolving the argument */
};

static struct dispatch_slots *dispatch_value_slots(const struct dispatch_value *value)
{
    return value->owner->context->slots;
}

static const void *dispatch_argument_target(struct dispatch_handle **handle, enum dispatch_flaw flaw)
{
    struct dispatch_handle *current = *handle;
    const void *target = current->target;
    if (!target) {
        ((resolve_fn *)rt_guest(RESOLVE_HANDLE_TARGET))(current);
        target = flaw == HANDLE_NOT_RELOADED ? current->target : (*handle)->target;
    }
    return target;
}

static _Bool dispatch_with_flaw(uint8_t *self, struct dispatch_value *value, struct dispatch_handle **handle,
                                void *argument, ptrdiff_t member_offset, enum dispatch_flaw flaw)
{
    with_slot_fn *with_slot = (with_slot_fn *)rt_guest(DISPATCH_WITH_SLOT);
    struct dispatch_slots *slots = dispatch_value_slots(value);
    uint8_t kind = value->kind;
    _Bool matched;
    if (kind >= FIRST_POINTER_COMPARED_KIND) {
        matched = slots->first == *handle;
    } else {
        struct dispatch_handle *early = slots->first;
        const void *target = dispatch_argument_target(handle, flaw);
        if (flaw == SLOTS_AFTER_RESOLVE)
            slots = dispatch_value_slots(value);
        matched = (flaw == FIRST_SLOT_BEFORE_RESOLVE ? early : slots->first)->target == target;
    }
    if (matched)
        return with_slot(self + member_offset, self, value, handle, argument, 1);

    if (flaw != SLOTS_KEPT_FOR_SECOND)
        slots = dispatch_value_slots(value);
    if (flaw != KIND_KEPT_FOR_SECOND)
        kind = value->kind;
    if (kind >= FIRST_POINTER_COMPARED_KIND) {
        matched = slots->second == *handle;
    } else {
        struct dispatch_handle *early = slots->second;
        const void *target = dispatch_argument_target(handle, flaw);
        matched = (flaw == SECOND_SLOT_BEFORE_RESOLVE ? early : slots->second)->target == target;
    }
    if (matched)
        return with_slot(self + member_offset, self, value, handle, argument, 0);
    return ((without_slot_fn *)rt_guest(DISPATCH_WITHOUT_SLOT))(self, value, handle, argument);
}

#define DISPATCH_MEMBER_0X68 0x68
#define DISPATCH_MEMBER_0X70 0x70

_Bool bb_mutant_slot_dispatch_0x68_first_slot_before_resolve(uint8_t *self, struct dispatch_value *value,
                                                             struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X68, FIRST_SLOT_BEFORE_RESOLVE);
}

_Bool bb_mutant_slot_dispatch_0x68_handle_not_reloaded(uint8_t *self, struct dispatch_value *value,
                                                       struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X68, HANDLE_NOT_RELOADED);
}

_Bool bb_mutant_slot_dispatch_0x68_slots_after_resolve(uint8_t *self, struct dispatch_value *value,
                                                       struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X68, SLOTS_AFTER_RESOLVE);
}

_Bool bb_mutant_slot_dispatch_0x68_slots_kept_for_second(uint8_t *self, struct dispatch_value *value,
                                                         struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X68, SLOTS_KEPT_FOR_SECOND);
}

_Bool bb_mutant_slot_dispatch_0x68_kind_kept_for_second(uint8_t *self, struct dispatch_value *value,
                                                        struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X68, KIND_KEPT_FOR_SECOND);
}

_Bool bb_mutant_slot_dispatch_0x68_second_slot_before_resolve(uint8_t *self, struct dispatch_value *value,
                                                              struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X68, SECOND_SLOT_BEFORE_RESOLVE);
}

_Bool bb_mutant_slot_dispatch_0x70_first_slot_before_resolve(uint8_t *self, struct dispatch_value *value,
                                                             struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X70, FIRST_SLOT_BEFORE_RESOLVE);
}

_Bool bb_mutant_slot_dispatch_0x70_handle_not_reloaded(uint8_t *self, struct dispatch_value *value,
                                                       struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X70, HANDLE_NOT_RELOADED);
}

_Bool bb_mutant_slot_dispatch_0x70_slots_after_resolve(uint8_t *self, struct dispatch_value *value,
                                                       struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X70, SLOTS_AFTER_RESOLVE);
}

_Bool bb_mutant_slot_dispatch_0x70_slots_kept_for_second(uint8_t *self, struct dispatch_value *value,
                                                         struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X70, SLOTS_KEPT_FOR_SECOND);
}

_Bool bb_mutant_slot_dispatch_0x70_kind_kept_for_second(uint8_t *self, struct dispatch_value *value,
                                                        struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X70, KIND_KEPT_FOR_SECOND);
}

_Bool bb_mutant_slot_dispatch_0x70_second_slot_before_resolve(uint8_t *self, struct dispatch_value *value,
                                                              struct dispatch_handle **handle, void *argument)
{
    return dispatch_with_flaw(self, value, handle, argument, DISPATCH_MEMBER_0X70, SECOND_SLOT_BEFORE_RESOLVE);
}

/* --- visits_base_pairs_and_entries_at_* ---------------------------------------------------- */

struct visited_entry {
    uint8_t unknown_0x0[0x8];
    struct visited_entry *previous;
    struct visited_entry *next;
    uint32_t state;
};

struct visit_list {
    uint8_t unknown_0x0[0x8];
    struct visited_entry *current;
};

struct visitor {
    uint8_t unknown_0x0[0x58];
    struct visit_list *list;
};

struct entry_array {
    struct visited_entry **data;
    uint64_t count;
};

#define PENDING_STATE_BITS 0x70000000u
#define LISTED_STATE_BIT 0x08000000u
#define VISIT_BASE 0x027fd2c0u
#define VISIT_PAIR 0x027fe500u
#define SECOND_PAIR_AFTER_FIRST 0x18
#define ENTRIES_AFTER_FIRST 0x30

typedef void visit_fn(void *object, struct visitor *visitor);

enum visit_flaw {
    ENTRIES_BEFORE_CALLS,  /* read the array pointer before the base and pair visits */
    ENTRIES_READ_ONCE,     /* keep the array pointer and its data across the entries */
    COUNT_EVERY_ENTRY,     /* read the count again for every entry */
};

static void visit_entry(struct visited_entry *entry, struct visitor *visitor)
{
    uint32_t state = entry->state + 1;
    entry->state = state;
    if (!(state & PENDING_STATE_BITS))
        return;
    entry->state = state & ~PENDING_STATE_BITS;
    if (!(state & LISTED_STATE_BIT))
        return;
    entry->next->previous = entry->previous;
    entry->previous->next = entry->next;
    entry->next = visitor->list->current->next;
    entry->previous = visitor->list->current;
    visitor->list->current->next = entry;
    visitor->list->current = entry;
}

static void visit_with_flaw(uint8_t *self, struct visitor *visitor, ptrdiff_t first_pair, enum visit_flaw flaw)
{
    struct entry_array **entries = (struct entry_array **)(self + first_pair + ENTRIES_AFTER_FIRST);
    struct entry_array *early = *entries;
    ((visit_fn *)rt_guest(VISIT_BASE))(self, visitor);
    ((visit_fn *)rt_guest(VISIT_PAIR))(self + first_pair, visitor);
    ((visit_fn *)rt_guest(VISIT_PAIR))(self + first_pair + SECOND_PAIR_AFTER_FIRST, visitor);
    struct entry_array *array = flaw == ENTRIES_BEFORE_CALLS ? early : *entries;
    if (!array || !array->count)
        return;
    uint64_t count = array->count;
    for (uint64_t index = 0; index < count; ++index) {
        struct entry_array *current = flaw == ENTRIES_READ_ONCE || flaw == ENTRIES_BEFORE_CALLS ? array : *entries;
        struct visited_entry *entry = current->data[index];
        if (entry)
            visit_entry(entry, visitor);
        if (flaw == COUNT_EVERY_ENTRY)
            count = (*entries)->count;
    }
}

#define FIRST_PAIR_0X70 0x70
#define FIRST_PAIR_0X78 0x78

void bb_mutant_visit_0x70_entries_before_calls(uint8_t *self, struct visitor *visitor)
{
    visit_with_flaw(self, visitor, FIRST_PAIR_0X70, ENTRIES_BEFORE_CALLS);
}

void bb_mutant_visit_0x70_entries_read_once(uint8_t *self, struct visitor *visitor)
{
    visit_with_flaw(self, visitor, FIRST_PAIR_0X70, ENTRIES_READ_ONCE);
}

void bb_mutant_visit_0x70_count_every_entry(uint8_t *self, struct visitor *visitor)
{
    visit_with_flaw(self, visitor, FIRST_PAIR_0X70, COUNT_EVERY_ENTRY);
}

void bb_mutant_visit_0x78_entries_before_calls(uint8_t *self, struct visitor *visitor)
{
    visit_with_flaw(self, visitor, FIRST_PAIR_0X78, ENTRIES_BEFORE_CALLS);
}

void bb_mutant_visit_0x78_entries_read_once(uint8_t *self, struct visitor *visitor)
{
    visit_with_flaw(self, visitor, FIRST_PAIR_0X78, ENTRIES_READ_ONCE);
}

void bb_mutant_visit_0x78_count_every_entry(uint8_t *self, struct visitor *visitor)
{
    visit_with_flaw(self, visitor, FIRST_PAIR_0X78, COUNT_EVERY_ENTRY);
}
