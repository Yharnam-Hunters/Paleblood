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

/* The heap the global at 0x057e7320 points at (its slot 0x68 frees) and the owner as
   game/engine/heap_buffer_owner_families.cpp's destructors see it: vtable, handle at +0x8, buffer
   at +0x20. A handle with its low bit clear points at a counted object (count: the low 22 bits of
   the dword at +0x20), which 0x027ee1e0 is told about after the count drops. */
#define MUTANT_GLOBAL_HEAP 0x057e7320u
#define MUTANT_HEAP_BUFFER_OWNER_VTABLE 0x055b5be0u
#define MUTANT_COUNTED_RELEASED 0x027ee1e0u
#define MUTANT_VTABLE_ADDRESS_POINT 0x10u
#define MUTANT_COUNTED_COUNT_MASK 0x3fffffu
#define MUTANT_HANDLE_TAG 1u
#define MUTANT_FIRST_BASE 0x38u
#define MUTANT_SECOND_BASE 0x48u

struct mutant_global_heap_vtable {
    void *unknown_slots[13];
    void (*free)(void *heap, void *data);
};
struct mutant_heap_buffer_owner {
    const uint8_t *vtable;
    uint64_t handle;
    uint8_t unknown_0x10[0x10];
    void *buffer;
};
struct mutant_counted_object {
    uint8_t unknown_0x0[0x20];
    uint32_t state;
};
typedef void mutant_counted_released_fn(struct mutant_counted_object *object);

static void mutant_free_to_global_heap(void *data)
{
    void *heap = *(void **)rt_guest(MUTANT_GLOBAL_HEAP);
    (*(const struct mutant_global_heap_vtable **)heap)->free(heap, data);
}

/* What each mutant gets wrong: the handle read before the free, the vtable stored after it, the
   whole state dword taken as the count, or the release call left out. */
enum {
    MUTANT_OWNER_HANDLE_SNAPSHOT = 1,
    MUTANT_OWNER_VTABLE_LATE = 2,
    MUTANT_OWNER_WHOLE_STATE = 4,
    MUTANT_OWNER_NO_RELEASE = 8,
};

static void mutant_destroy_heap_buffer_owner(uint8_t *self, uint64_t base, int wrong)
{
    struct mutant_heap_buffer_owner *owner = (struct mutant_heap_buffer_owner *)(self - base);
    const uint8_t *vtable = (const uint8_t *)rt_guest(MUTANT_HEAP_BUFFER_OWNER_VTABLE) + MUTANT_VTABLE_ADDRESS_POINT;
    uint64_t handle = owner->handle;
    if (!(wrong & MUTANT_OWNER_VTABLE_LATE))
        owner->vtable = vtable;
    mutant_free_to_global_heap(owner->buffer);
    if (wrong & MUTANT_OWNER_VTABLE_LATE)
        owner->vtable = vtable;
    if (!(wrong & MUTANT_OWNER_HANDLE_SNAPSHOT))
        handle = owner->handle;
    if (!handle)
        return;
    if (handle & MUTANT_HANDLE_TAG) {
        owner->handle = handle - 1;
        return;
    }
    struct mutant_counted_object *object = (struct mutant_counted_object *)(uintptr_t)handle;
    uint32_t count = (wrong & MUTANT_OWNER_WHOLE_STATE) ? object->state : object->state & MUTANT_COUNTED_COUNT_MASK;
    if (!count)
        return;
    object->state -= 1;
    if (!(wrong & MUTANT_OWNER_NO_RELEASE))
        ((mutant_counted_released_fn *)rt_guest(MUTANT_COUNTED_RELEASED))(object);
}

/* Four wrong versions of each of the three destructor families, by the base they are entered at. */
#define MUTANT_HEAP_BUFFER_OWNER(suffix, base)                                                       \
    void bb_mutant_heap_buffer_owner_handle_snapshot_##suffix(uint8_t *self)                         \
    {                                                                                                \
        mutant_destroy_heap_buffer_owner(self, base, MUTANT_OWNER_HANDLE_SNAPSHOT);                  \
    }                                                                                                \
    void bb_mutant_heap_buffer_owner_vtable_late_##suffix(uint8_t *self)                             \
    {                                                                                                \
        mutant_destroy_heap_buffer_owner(self, base, MUTANT_OWNER_VTABLE_LATE);                      \
    }                                                                                                \
    void bb_mutant_heap_buffer_owner_whole_state_##suffix(uint8_t *self)                             \
    {                                                                                                \
        mutant_destroy_heap_buffer_owner(self, base, MUTANT_OWNER_WHOLE_STATE);                      \
    }                                                                                                \
    void bb_mutant_heap_buffer_owner_no_release_##suffix(uint8_t *self)                              \
    {                                                                                                \
        mutant_destroy_heap_buffer_owner(self, base, MUTANT_OWNER_NO_RELEASE);                       \
    }

MUTANT_HEAP_BUFFER_OWNER(directly, 0u)
MUTANT_HEAP_BUFFER_OWNER(base_0x38, MUTANT_FIRST_BASE)
MUTANT_HEAP_BUFFER_OWNER(base_0x48, MUTANT_SECOND_BASE)

/* Frees a null pointer to the global heap whatever it is given: wrong for frees_to_global_heap. */
void bb_mutant_global_heap_frees_null(void *data)
{
    (void)data;
    mutant_free_to_global_heap(0);
}
