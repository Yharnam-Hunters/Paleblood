/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Control mutants for three engine families (game/engine/debug_menu_entry_family.cpp,
   text_for_slot_family.cpp, counted_member_release_family.cpp; game/engine/family_cases.py
   MUTANTS): each is the right body with one read moved to the other side of a call, so the cases
   whose stub changes that value during the call must catch it on every member. */
#include <stddef.h>
#include <stdint.h>

#include "runtime/guest.h"

/* --- removes_debug_menu_entry_keyed_by_object ------------------------------------------------ */

#define MENU_MANAGER 0x058b3570u
#define ENTRY_KEY_NAME 0x04dd85f0u
#define MENU_REMOVE 0x013a33a0u
#define NAME_CONSTRUCT 0x013aeaf0u
#define NAME_DESTROY 0x013aebc0u
#define MENU_REMOVE_FLAG 1

struct menu_key {
    const void *owner;
    void *name;
};

typedef void name_construct_fn(void **name, const void *text);
typedef void name_destroy_fn(void **name);
typedef void menu_remove_fn(void *manager, struct menu_key *key, int32_t flag_a, int32_t flag_b);

/* Reads the manager again after interning the name (the copies read it once, before). */
void bb_mutant_menu_entry_manager_reloaded(const void *object)
{
    void *volatile *manager = (void *volatile *)rt_guest(MENU_MANAGER);
    if (!*manager) return;
    struct menu_key key;
    key.owner = object;
    ((name_construct_fn *)rt_guest(NAME_CONSTRUCT))(&key.name, rt_guest(ENTRY_KEY_NAME));
    ((menu_remove_fn *)rt_guest(MENU_REMOVE))(*manager, &key, MENU_REMOVE_FLAG, MENU_REMOVE_FLAG);
    ((name_destroy_fn *)rt_guest(NAME_DESTROY))(&key.name);
}

/* --- formats_one_of_three_texts_for_slot_0x80 ------------------------------------------------ */

#define THREAD_HEAP_OFFSET 0x057e4568u
#define CREATE_THREAD_HEAP 0x0247d250u
#define FATAL_ERROR 0x024b55b0u
#define INVALID_HEAP_MESSAGE 0x04b37dc0u
#define INVALID_HEAP_LINE 0x3e
#define WIDE_STRING_FORMAT 0x02b62d00u
#define FLAGGED_TEXT 0x04dbec16u
#define QUERIED_TEXT 0x04dbdf48u
#define DEFAULT_TEXT 0x04dbdec8u
#define ALLOCATOR_INFO_VALID 0x20u
#define ALLOCATOR_INFO_SIZE 0x10
#define INLINE_CAPACITY 8u

struct text_allocator;
struct text_allocator_vtable {
    void *unknown_slots[4];
    void (*info)(uint8_t *out, struct text_allocator *, int32_t);
    void *unknown_slots_1[9];
    void (*free)(struct text_allocator *, void *data);
};
struct text_allocator {
    const struct text_allocator_vtable *vtable;
};
struct thread_heap {
    uint8_t unknown_0x00[0x28];
    struct text_allocator allocator;
};
struct wide_text {
    uint64_t unknown_0x00;
    union {
        uint16_t inline_text[INLINE_CAPACITY];
        uint16_t *heap;
    };
    uint64_t size;
    uint64_t capacity;
    struct text_allocator *allocator;
    uint8_t owns;
};
struct text_source;
struct text_source_vtable {
    void *unknown_slots[4];
    _Bool (*query)(struct text_source *);
};
struct text_source {
    const struct text_source_vtable *vtable;
    uint8_t unknown_0x08[0x60];
    uint8_t enabled;
};
struct text_target;
typedef void take_text_fn(struct text_target *, const uint16_t *text);
struct text_target_vtable {
    void *unknown_slots[16];
    take_text_fn *take_text;
};
struct text_target {
    const struct text_target_vtable *vtable;
};

typedef void create_thread_heap_fn(void);
typedef void fatal_error_fn(const char *file, int32_t line, const char *format, ...);
typedef void wide_string_format_fn(struct wide_text *, const void *format, ...);

static struct thread_heap **thread_heap_slot(void)
{
    unsigned char *thread;
    __asm__ volatile("movq %%gs:0, %0" : "=r"(thread));
    return (struct thread_heap **)(thread + *(const int64_t *)rt_guest(THREAD_HEAP_OFFSET));
}

/* Reads the target's slot before formatting the text (the copies read it after). */
void bb_mutant_text_slot_before_format(struct text_source *self, struct text_target *target, int32_t flagged)
{
    if (!self->enabled) return;
    if (!*thread_heap_slot()) ((create_thread_heap_fn *)rt_guest(CREATE_THREAD_HEAP))();
    struct text_allocator *allocator = &(*thread_heap_slot())->allocator;
    uint8_t info[ALLOCATOR_INFO_SIZE];
    allocator->vtable->info(info, allocator, 0);
    if (!(info[0] & ALLOCATOR_INFO_VALID))
        ((fatal_error_fn *)rt_guest(FATAL_ERROR))(NULL, INVALID_HEAP_LINE, (const char *)rt_guest(INVALID_HEAP_MESSAGE));
    struct wide_text text;
    text.allocator = allocator;
    text.capacity = INLINE_CAPACITY - 1;
    text.size = 0;
    text.inline_text[0] = 0;
    text.owns = 1;
    const void *format = rt_guest(flagged ? FLAGGED_TEXT : self->vtable->query(self) ? QUERIED_TEXT : DEFAULT_TEXT);
    take_text_fn *take_text = target->vtable->take_text;
    ((wide_string_format_fn *)rt_guest(WIDE_STRING_FORMAT))(&text, format);
    take_text(target, text.capacity >= INLINE_CAPACITY ? text.heap : text.inline_text);
    if (text.capacity >= INLINE_CAPACITY) text.allocator->vtable->free(text.allocator, text.heap);
}

/* --- releases_counted_member_at_0x30_then_base ----------------------------------------------- */

#define RELEASE_BASE_MEMBERS 0x02751f90u
#define COUNT_MASK 0x7fffu
#define SECOND_COUNT_SHIFT 15
#define FIRST_COUNT_FLAG 0x40000000u

struct counted_object;
struct counted_vtable {
    void *unknown_slots[3];
    void (*first_count_ended)(struct counted_object *);
    void (*second_count_ended)(struct counted_object *);
};
struct counted_object {
    const struct counted_vtable *vtable;
    uint32_t counts;
};
struct counted_owner {
    uint8_t unknown_0x00[0x28];
    uint64_t cleared_0x28;
    struct counted_object *counted;
};

typedef void release_base_fn(struct counted_owner *);

/* Keeps the word it stored before slot 0x18 instead of reading it again after the slot. */
void bb_mutant_counted_release_count_not_reloaded(struct counted_owner *self)
{
    struct counted_object *object = self->counted;
    if (object) {
        const uint32_t old = object->counts;
        const uint32_t first = (old + COUNT_MASK) & COUNT_MASK;
        uint32_t counts = (old & ~COUNT_MASK) | first;
        object->counts = counts;
        if (first == 0 && (old & FIRST_COUNT_FLAG)) {
            object->vtable->first_count_ended(object);
            counts &= ~FIRST_COUNT_FLAG;
            object->counts = counts;
        }
        const uint32_t second = ((counts >> SECOND_COUNT_SHIFT) + COUNT_MASK) & COUNT_MASK;
        object->counts = (counts & ~(COUNT_MASK << SECOND_COUNT_SHIFT)) | (second << SECOND_COUNT_SHIFT);
        if (second == 0) {
            object->counts = counts | (COUNT_MASK << SECOND_COUNT_SHIFT);
            object->vtable->second_count_ended(object);
        }
        self->counted = NULL;
    }
    self->cleared_0x28 = 0;
    ((release_base_fn *)rt_guest(RELEASE_BASE_MEMBERS))(self);
}
