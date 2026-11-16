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

/* forwards_to_referenced_node_slot_0x28 (game/engine/virtual_method_families.cpp): a reference's
   16-bit kind (+0x8) and signed index (+0xa) resolved by 0x02c4f560 into a node whose vtable slot
   0x28 is called. */
#define MUTANT_RESOLVE_NODE 0x02c4f560u
struct mutant_node_reference {
    uint8_t unknown_0x00[0x8];
    uint16_t kind;
    int16_t index;
};
struct mutant_node;
typedef void *mutant_node_method_fn(struct mutant_node *node, void *context, void *argument, uint8_t flag);
struct mutant_node_vtable {
    void *unknown_slots[5];
    mutant_node_method_fn *method_0x28;
};
struct mutant_node {
    const struct mutant_node_vtable *vtable;
};
typedef struct mutant_node *mutant_resolve_node_fn(void *context, uint32_t kind, uint32_t index);

static void *mutant_node_forward(const struct mutant_node_reference *reference, void *context, void *argument,
                                 uint8_t flag, uint32_t index)
{
    mutant_resolve_node_fn *resolve = (mutant_resolve_node_fn *)rt_guest(MUTANT_RESOLVE_NODE);
    struct mutant_node *node = resolve(context, reference->kind, index);
    return node->vtable->method_0x28(node, context, argument, flag);
}

/* Widens the index without its sign: wrong for forwards_to_referenced_node_slot_0x28. */
void *bb_mutant_node_forward_unsigned_index(const struct mutant_node_reference *reference, void *context,
                                            void *argument, uint8_t flag)
{
    return mutant_node_forward(reference, context, argument, flag, (uint16_t)reference->index);
}

/* Passes no flag on: wrong for forwards_to_referenced_node_slot_0x28. */
void *bb_mutant_node_forward_drops_flag(const struct mutant_node_reference *reference, void *context,
                                        void *argument, uint8_t flag)
{
    (void)flag;
    return mutant_node_forward(reference, context, argument, 0, (uint32_t)(int32_t)reference->index);
}

/* posts_event_9_when_accepted: the base handler 0x0139c240, the stop bits of the event's flag
   byte +0x1b, the handler's slot 0x78, and the post 0x0139bc30. */
#define MUTANT_HANDLE_EVENT_BASE 0x0139c240u
#define MUTANT_POST_HANDLER_EVENT 0x0139bc30u
#define MUTANT_EVENT_FLAGS_HIGH_BYTE 0x1b
#define MUTANT_EVENT_STOP_BITS 0x90u
#define MUTANT_ACCEPTED_EVENT_KIND 9
#define MUTANT_ANSWER_BYTE 0xffu
struct mutant_handler;
typedef uint64_t mutant_accepts_fn(struct mutant_handler *handler, uint8_t *event);
struct mutant_handler_vtable {
    void *unknown_slots[15];
    mutant_accepts_fn *accepts;
};
struct mutant_handler {
    const struct mutant_handler_vtable *vtable;
};
typedef void mutant_handle_event_fn(struct mutant_handler *handler, void *source, uint8_t *event);
typedef void mutant_post_event_fn(struct mutant_handler *handler, int32_t kind, void *source, uint64_t value,
                                  uint8_t *event);

static void mutant_post_event(struct mutant_handler *handler, void *source, uint8_t *event, uint64_t answer)
{
    if (answer == 0)
        return;
    ((mutant_post_event_fn *)rt_guest(MUTANT_POST_HANDLER_EVENT))(handler, MUTANT_ACCEPTED_EVENT_KIND, source, 0,
                                                                  event);
}

/* Reads the flag byte before the base handler (a control for the order claim): wrong for
   posts_event_9_when_accepted, which reads it after. */
void bb_mutant_event_flags_read_before_base(struct mutant_handler *handler, void *source, uint8_t *event)
{
    const uint8_t flags = event[MUTANT_EVENT_FLAGS_HIGH_BYTE];
    ((mutant_handle_event_fn *)rt_guest(MUTANT_HANDLE_EVENT_BASE))(handler, source, event);
    if (flags & MUTANT_EVENT_STOP_BITS)
        return;
    mutant_post_event(handler, source, event, handler->vtable->accepts(handler, event) & MUTANT_ANSWER_BYTE);
}

/* Tests the whole answer register instead of its byte: wrong for posts_event_9_when_accepted. */
void bb_mutant_event_accepts_whole_register(struct mutant_handler *handler, void *source, uint8_t *event)
{
    ((mutant_handle_event_fn *)rt_guest(MUTANT_HANDLE_EVENT_BASE))(handler, source, event);
    if (event[MUTANT_EVENT_FLAGS_HIGH_BYTE] & MUTANT_EVENT_STOP_BITS)
        return;
    mutant_post_event(handler, source, event, handler->vtable->accepts(handler, event));
}

/* syncs_selected_id_and_index and selects_entry_id_by_index: a selector (flags +0xc, id +0x20,
   id slot +0x28, list +0x30) over 24-byte entries with an id at +0x10. */
#define MUTANT_APPLY_SELECTED_INDEX 0x02752100u
#define MUTANT_APPLY_SELECTED_ID 0x02762360u
struct mutant_id_entry {
    uint8_t unknown_0x00[0x10];
    uint32_t id;
    uint8_t unknown_0x14[0x4];
};
struct mutant_id_list {
    uint8_t unknown_0x00[0x18];
    struct mutant_id_entry *begin;
    struct mutant_id_entry *end;
};
struct mutant_id_selector {
    uint8_t unknown_0x00[0xc];
    uint8_t flags;
    uint8_t unknown_0x0d[0x13];
    uint32_t id;
    uint32_t *id_slot;
    struct mutant_id_list *list;
};
typedef void mutant_apply_index_fn(struct mutant_id_selector *selector, int32_t index);
typedef void mutant_apply_id_fn(struct mutant_id_selector *selector);

static void mutant_sync_selected(struct mutant_id_selector *selector, int last, int store)
{
    const uint32_t *slot = selector->id_slot;
    if (!slot || *slot == selector->id)
        return;
    const uint32_t id = *slot;
    if (store)
        selector->id = id;
    const uint32_t count = (uint32_t)(selector->list->end - selector->list->begin);
    int32_t index = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (selector->list->begin[i].id == id) {
            index = (int32_t)i;
            if (!last)
                break;
        }
    }
    ((mutant_apply_index_fn *)rt_guest(MUTANT_APPLY_SELECTED_INDEX))(selector, index);
}

/* Passes the last entry with the id on: wrong for syncs_selected_id_and_index (the first). */
void bb_mutant_selected_id_last_match(struct mutant_id_selector *selector)
{
    mutant_sync_selected(selector, 1, 1);
}

/* Does not keep the new id: wrong for syncs_selected_id_and_index. */
void bb_mutant_selected_id_not_stored(struct mutant_id_selector *selector)
{
    mutant_sync_selected(selector, 0, 0);
}

static void mutant_select_entry(struct mutant_id_selector *selector, uint32_t index, int inclusive, int lock)
{
    if (lock && (selector->flags & 1u))
        return;
    const uint64_t count = (uint64_t)(selector->list->end - selector->list->begin);
    if (inclusive ? (uint64_t)index > count : (uint64_t)index >= count)
        return;
    const uint32_t id = selector->list->begin[index].id;
    selector->id = id;
    *selector->id_slot = id;
    ((mutant_apply_id_fn *)rt_guest(MUTANT_APPLY_SELECTED_ID))(selector);
}

/* Also selects the index equal to the count: wrong for selects_entry_id_by_index. */
void bb_mutant_entry_index_inclusive(struct mutant_id_selector *selector, uint32_t index)
{
    mutant_select_entry(selector, index, 1, 1);
}

/* Selects while locked: wrong for selects_entry_id_by_index. */
void bb_mutant_entry_index_ignores_lock(struct mutant_id_selector *selector, uint32_t index)
{
    mutant_select_entry(selector, index, 0, 0);
}

/* releases_counted_reference_at_0x20: the counted object at +0x20 (counts at +0x8: strong bits
   0-14, weak bits 15-29, alive bit 30; destroy slot 0x18, free slot 0x20), then +0x28 cleared and
   0x02759cc0. */
#define MUTANT_RELEASE_BASE 0x02759cc0u
#define MUTANT_COUNT_MASK 0x7fffu
#define MUTANT_WEAK_SHIFT 15
#define MUTANT_ALIVE 0x40000000u
#define MUTANT_KEEP_WITH_WEAK 0xc0007fffu
struct mutant_counted;
typedef void mutant_counted_fn(struct mutant_counted *counted);
struct mutant_counted_vtable {
    void *unknown_slots[3];
    mutant_counted_fn *destroy;
    mutant_counted_fn *free;
};
struct mutant_counted {
    const struct mutant_counted_vtable *vtable;
    uint32_t counts;
};
struct mutant_counted_holder {
    uint8_t unknown_0x00[0x20];
    struct mutant_counted *counted;
    uint64_t value_0x28;
};
typedef void mutant_release_base_fn(struct mutant_counted_holder *holder);

static void mutant_release(struct mutant_counted_holder *holder, int reload, int weak_marked, int clear_pointer)
{
    struct mutant_counted *counted = holder->counted;
    if (counted) {
        const uint32_t old = counted->counts;
        uint32_t counts = (old & ~MUTANT_COUNT_MASK) | ((old - 1) & MUTANT_COUNT_MASK);
        counted->counts = counts;
        if (!(counts & MUTANT_COUNT_MASK) && (old & MUTANT_ALIVE)) {
            counted->vtable->destroy(counted);
            counts = (reload ? counted->counts : counts) & ~MUTANT_ALIVE;
            counted->counts = counts;
        }
        const uint32_t weak = ((counts >> MUTANT_WEAK_SHIFT) - 1) & MUTANT_COUNT_MASK;
        counted->counts = (counts & MUTANT_KEEP_WITH_WEAK) | (weak << MUTANT_WEAK_SHIFT);
        if (!weak) {
            if (weak_marked)
                counted->counts = counts | (MUTANT_COUNT_MASK << MUTANT_WEAK_SHIFT);
            counted->vtable->free(counted);
        }
        if (clear_pointer)
            holder->counted = 0;
    }
    holder->value_0x28 = 0;
    ((mutant_release_base_fn *)rt_guest(MUTANT_RELEASE_BASE))(holder);
}

/* Keeps the counts from before the destroy (a control for the order claim): wrong for
   releases_counted_reference_at_0x20, which reads them again. */
void bb_mutant_counted_release_no_reload(struct mutant_counted_holder *holder)
{
    mutant_release(holder, 0, 1, 1);
}

/* Leaves the weak count at zero when freeing: wrong for releases_counted_reference_at_0x20,
   which sets every weak bit. */
void bb_mutant_counted_release_weak_zero(struct mutant_counted_holder *holder)
{
    mutant_release(holder, 1, 0, 1);
}

/* Keeps the pointer at +0x20: wrong for releases_counted_reference_at_0x20. */
void bb_mutant_counted_release_keeps_pointer(struct mutant_counted_holder *holder)
{
    mutant_release(holder, 1, 1, 0);
}

/* registers_new_entry_at_0x30: an entry from the function the global 0x056c45c8 points at,
   constructed (0x02aebf90), given the value (0x02aebf60) and registered (0x02aeb740). */
#define MUTANT_ENTRY_ALLOCATOR 0x056c45c8u
#define MUTANT_CONSTRUCT_ENTRY 0x02aebf90u
#define MUTANT_SET_ENTRY_VALUE 0x02aebf60u
#define MUTANT_REGISTER_ENTRY 0x02aeb740u
#define MUTANT_ENTRY_SIZE 0x20u
struct mutant_registrant {
    uint8_t unknown_0x00[0x28];
    void *registry;
    void *entry;
};
typedef void *mutant_allocate_fn(uint64_t size);
typedef void mutant_entry_fn(void *entry);
typedef void mutant_set_value_fn(void *entry, void *value);
typedef void mutant_register_fn(void *registry, struct mutant_registrant *self, void *entry);

static void mutant_register(struct mutant_registrant *self, void *value, void *registry, int reload, int set_null)
{
    self->registry = registry;
    void *entry = (*(mutant_allocate_fn **)rt_guest(MUTANT_ENTRY_ALLOCATOR))(MUTANT_ENTRY_SIZE);
    if (entry)
        ((mutant_entry_fn *)rt_guest(MUTANT_CONSTRUCT_ENTRY))(entry);
    self->entry = entry;
    if (entry || set_null)
        ((mutant_set_value_fn *)rt_guest(MUTANT_SET_ENTRY_VALUE))(entry, value);
    if (reload)
        ((mutant_register_fn *)rt_guest(MUTANT_REGISTER_ENTRY))(self->registry, self, self->entry);
    else
        ((mutant_register_fn *)rt_guest(MUTANT_REGISTER_ENTRY))(registry, self, entry);
}

/* Registers the owner and entry from before the value is set (a control for the order claim):
   wrong for registers_new_entry_at_0x30, which reads both again. */
void bb_mutant_entry_registration_no_reload(struct mutant_registrant *self, void *value, void *registry)
{
    mutant_register(self, value, registry, 0, 1);
}

/* Does not set the value of a missing entry: wrong for registers_new_entry_at_0x30, which sets it
   on null too. */
void bb_mutant_entry_registration_skips_null_entry(struct mutant_registrant *self, void *value, void *registry)
{
    mutant_register(self, value, registry, 1, 0);
}
