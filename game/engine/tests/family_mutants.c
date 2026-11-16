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

/* fills_owner_block_with_two_addresses and fills_owner_block_with_one_address
   (game/engine/owner_block_families.cpp): the addresses 0x05acd45c and 0x05acd1bc in the block's
   first three pointers, 12 qwords cleared from +0x18, the owner at +0x78, 0 returned. */
#define MUTANT_BLOCK_ADDRESS_A 0x05acd45cu
#define MUTANT_BLOCK_ADDRESS_B 0x05acd1bcu
#define MUTANT_BLOCK_CLEARED 12
struct mutant_owner_block {
    const void *first;
    const void *second;
    const void *third;
    uint64_t cleared[MUTANT_BLOCK_CLEARED];
    void *owner;
};

static int32_t mutant_fill_block(void *owner, struct mutant_owner_block *block, uint32_t third, int keep_last)
{
    block->first = rt_guest(MUTANT_BLOCK_ADDRESS_A);
    block->second = rt_guest(MUTANT_BLOCK_ADDRESS_A);
    block->third = rt_guest(third);
    for (int i = 0; i < MUTANT_BLOCK_CLEARED - keep_last; ++i)
        block->cleared[i] = 0;
    block->owner = owner;
    return 0;
}

/* Stores 0x05acd45c in the third pointer too: wrong for fills_owner_block_with_two_addresses. */
int32_t bb_mutant_owner_block_one_address(void *owner, struct mutant_owner_block *block)
{
    return mutant_fill_block(owner, block, MUTANT_BLOCK_ADDRESS_A, 0);
}

/* Stores 0x05acd1bc in the third pointer: wrong for fills_owner_block_with_one_address. */
int32_t bb_mutant_owner_block_two_addresses(void *owner, struct mutant_owner_block *block)
{
    return mutant_fill_block(owner, block, MUTANT_BLOCK_ADDRESS_B, 0);
}

/* Leaves the last cleared qword (+0x70) alone: wrong for both block families. */
int32_t bb_mutant_owner_block_keeps_0x70(void *owner, struct mutant_owner_block *block)
{
    return mutant_fill_block(owner, block, MUTANT_BLOCK_ADDRESS_A, 1);
}

/* Fills the block right and returns 1: wrong for both block families, which return 0. */
int32_t bb_mutant_owner_block_returns_one(void *owner, struct mutant_owner_block *block)
{
    mutant_fill_block(owner, block, MUTANT_BLOCK_ADDRESS_A, 0);
    return 1;
}

/* updates_and_calls_slot_0x38_unless_refused (game/engine/forwarding_method_families.cpp): the
   source at +0xe0 (target +0x18, value +0x24) checked by 0x01c3f3b0, the update 0x020ce2f0, then
   the object's slot 0x38. */
#define MUTANT_CHECK_UPDATE_TARGET 0x01c3f3b0u
#define MUTANT_UPDATE_OBJECT 0x020ce2f0u
struct mutant_update_source {
    uint8_t unknown_0x00[0x18];
    void *target;
    uint8_t unknown_0x20[0x4];
    uint32_t value;
};
struct mutant_updated;
struct mutant_updated_vtable {
    void *unknown_slots[7];
    void (*method_0x38)(struct mutant_updated *);
};
struct mutant_updated {
    const struct mutant_updated_vtable *vtable;
    uint8_t unknown_0x08[0xd8];
    struct mutant_update_source *source;
};
typedef uint64_t mutant_check_target_fn(void *target, uint32_t value);
typedef void mutant_update_fn(struct mutant_updated *object);

static void mutant_update(struct mutant_updated *object, int whole_register, int table_first, int stop_without_target)
{
    struct mutant_update_source *source = object->source;
    if (source->target == 0) {
        if (stop_without_target)
            return;
    } else {
        uint64_t answer = ((mutant_check_target_fn *)rt_guest(MUTANT_CHECK_UPDATE_TARGET))(source->target, source->value);
        if ((whole_register ? answer : (uint32_t)answer) == 0)
            return;
    }
    const struct mutant_updated_vtable *vtable = object->vtable;
    ((mutant_update_fn *)rt_guest(MUTANT_UPDATE_OBJECT))(object);
    (table_first ? vtable : object->vtable)->method_0x38(object);
}

/* Tests the check's whole register: wrong for updates_and_calls_slot_0x38_unless_refused, which
   tests its low 32 bits. */
void bb_mutant_update_check_whole_register(struct mutant_updated *object)
{
    mutant_update(object, 1, 0, 0);
}

/* Reads the table before the update (a control for the order claim): wrong for
   updates_and_calls_slot_0x38_unless_refused, which reads it after. */
void bb_mutant_update_table_read_first(struct mutant_updated *object)
{
    mutant_update(object, 0, 1, 0);
}

/* Stops when the source has no target: wrong for updates_and_calls_slot_0x38_unless_refused,
   which then updates. */
void bb_mutant_update_stops_without_target(struct mutant_updated *object)
{
    mutant_update(object, 0, 0, 1);
}

/* adopts_buffer_at_0x10_after_slot_0x78: slot 0x78 told 0, the count at +0x20, the old buffer at
   +0x10 freed from 8 bytes before it by the function the global 0x056c45d0 points at, then the
   new buffer and the count at +0x18 and +0x1c. */
#define MUTANT_FREE_BLOCK_GLOBAL 0x056c45d0u
#define MUTANT_LOADED_FREE_BLOCK 0x02b11cb0u
#define MUTANT_BUFFER_HEADER 8
struct mutant_buffer_holder;
struct mutant_buffer_holder_vtable {
    void *unknown_slots[15];
    void (*method_0x78)(struct mutant_buffer_holder *, uint32_t);
};
struct mutant_buffer_holder {
    const struct mutant_buffer_holder_vtable *vtable;
    uint8_t unknown_0x08[0x8];
    uint8_t *buffer;
    uint32_t count_0x18;
    uint32_t count_0x1c;
    uint32_t count_0x20;
};
typedef void mutant_free_block_fn(void *block);

static void mutant_adopt(struct mutant_buffer_holder *holder, uint8_t *buffer, uint32_t count, int early,
                         int header, int fixed_free)
{
    uint8_t *old = holder->buffer;
    if (early)
        holder->count_0x20 = count;
    holder->vtable->method_0x78(holder, 0);
    if (!early) {
        holder->count_0x20 = count;
        old = holder->buffer;
    }
    if (old) {
        mutant_free_block_fn *free_block = fixed_free ? (mutant_free_block_fn *)rt_guest(MUTANT_LOADED_FREE_BLOCK)
                                                      : *(mutant_free_block_fn **)rt_guest(MUTANT_FREE_BLOCK_GLOBAL);
        free_block(old - header);
    }
    holder->buffer = buffer;
    holder->count_0x18 = count;
    holder->count_0x1c = count;
}

/* Reads the old buffer and stores the count at +0x20 before slot 0x78 (a control for the order
   claim): wrong for adopts_buffer_at_0x10_after_slot_0x78, which does both after it. */
void bb_mutant_adopt_buffer_read_first(struct mutant_buffer_holder *holder, uint8_t *buffer, uint32_t count)
{
    mutant_adopt(holder, buffer, count, 1, MUTANT_BUFFER_HEADER, 0);
}

/* Frees the old buffer's own address: wrong for adopts_buffer_at_0x10_after_slot_0x78, which frees
   from 8 bytes before it. */
void bb_mutant_adopt_frees_buffer_itself(struct mutant_buffer_holder *holder, uint8_t *buffer, uint32_t count)
{
    mutant_adopt(holder, buffer, count, 0, 0, 0);
}

/* Calls the function the image loads into the global instead of reading the global: wrong for
   adopts_buffer_at_0x10_after_slot_0x78. */
void bb_mutant_adopt_fixed_free_function(struct mutant_buffer_holder *holder, uint8_t *buffer, uint32_t count)
{
    mutant_adopt(holder, buffer, count, 0, MUTANT_BUFFER_HEADER, 1);
}

/* destroys_with_0x0139bf90_then_deletes: 0x0139bf90, then operator delete (0x02fbe588). */
#define MUTANT_DESTROY_OBJECT 0x0139bf90u
#define MUTANT_OPERATOR_DELETE 0x02fbe588u
typedef void mutant_object_fn(void *object);

/* Destroys without deleting: wrong for destroys_with_0x0139bf90_then_deletes. */
void bb_mutant_destroy_without_delete(void *object)
{
    ((mutant_object_fn *)rt_guest(MUTANT_DESTROY_OBJECT))(object);
}

/* Deletes without destroying: wrong for destroys_with_0x0139bf90_then_deletes. */
void bb_mutant_delete_without_destroy(void *object)
{
    ((mutant_object_fn *)rt_guest(MUTANT_OPERATOR_DELETE))(object);
}

/* Deletes, then destroys: wrong for destroys_with_0x0139bf90_then_deletes. */
void bb_mutant_delete_then_destroy(void *object)
{
    ((mutant_object_fn *)rt_guest(MUTANT_OPERATOR_DELETE))(object);
    ((mutant_object_fn *)rt_guest(MUTANT_DESTROY_OBJECT))(object);
}

/* clears_0x28_and_passes_0x18_to_0x027286b0: +0x28 cleared, then the object at +0x18, when there
   is one, passed to 0x027286b0. */
#define MUTANT_PASS_TARGET 0x027286b0u
struct mutant_cleared_holder {
    uint8_t unknown_0x00[0x18];
    void *target;
    uint8_t unknown_0x20[0x8];
    uint64_t cleared;
};

/* Clears +0x28 after passing the target on: wrong for clears_0x28_and_passes_0x18_to_0x027286b0,
   whose callee sees it clear. */
void bb_mutant_clear_after_passing(struct mutant_cleared_holder *holder)
{
    if (holder->target)
        ((mutant_object_fn *)rt_guest(MUTANT_PASS_TARGET))(holder->target);
    holder->cleared = 0;
}

/* Passes a null target on too: wrong for clears_0x28_and_passes_0x18_to_0x027286b0. */
void bb_mutant_clear_passes_null_target(struct mutant_cleared_holder *holder)
{
    holder->cleared = 0;
    ((mutant_object_fn *)rt_guest(MUTANT_PASS_TARGET))(holder->target);
}

/* passes_float_to_0x01b5f7f0_when_0x010edc70_agrees: the float passed on when 0x010edc70's byte
   answer is not 0. */
#define MUTANT_AGREES 0x010edc70u
#define MUTANT_TAKE_FLOAT 0x01b5f7f0u
typedef uint64_t mutant_agrees_fn(void *object);
typedef void mutant_take_float_fn(void *object, float value);

/* Tests the answer's whole register: wrong for passes_float_to_0x01b5f7f0_when_0x010edc70_agrees,
   which tests its low byte. */
void bb_mutant_float_check_whole_register(void *object, float value)
{
    if (((mutant_agrees_fn *)rt_guest(MUTANT_AGREES))(object))
        ((mutant_take_float_fn *)rt_guest(MUTANT_TAKE_FLOAT))(object, value);
}

/* Passes the float on whatever the answer: wrong for
   passes_float_to_0x01b5f7f0_when_0x010edc70_agrees. */
void bb_mutant_float_passed_unconditionally(void *object, float value)
{
    ((mutant_agrees_fn *)rt_guest(MUTANT_AGREES))(object);
    ((mutant_take_float_fn *)rt_guest(MUTANT_TAKE_FLOAT))(object, value);
}
