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

/* The allocator forwarders (game/engine/allocator_forward_and_equality_families.cpp): the sized
   free (0x02fa1c20) and the allocation (0x02fa1b80) of the allocator the global 0x05ac8288 points
   at. Each mutant below gets one argument wrong. */
#define FORWARD_ALLOCATOR_GLOBAL 0x05ac8288u
#define FORWARD_ALLOCATE 0x02fa1b80u
#define FORWARD_SIZED_FREE 0x02fa1c20u
#define FORWARD_ALIGNMENT 8u
#define FORWARD_WRONG_ALIGNMENT 16u
#define FORWARD_SMALL_SIZE 8u
#define FORWARD_WRONG_SIZE 16u
typedef void *forward_allocate_fn(void *allocator, uint64_t size, uint64_t alignment);
typedef void forward_sized_free_fn(void *allocator, void *block, uint64_t size, uint64_t alignment);

static void *forward_allocator(void)
{
    return *(void *volatile *)rt_guest(FORWARD_ALLOCATOR_GLOBAL);
}

static void *forward_allocate(void *allocator, uint64_t size, uint64_t alignment)
{
    return ((forward_allocate_fn *)rt_guest(FORWARD_ALLOCATE))(allocator, size, alignment);
}

static void forward_sized_free(void *allocator, void *block, uint64_t size, uint64_t alignment)
{
    ((forward_sized_free_fn *)rt_guest(FORWARD_SIZED_FREE))(allocator, block, size, alignment);
}

/* Alignment 16: wrong for frees_8_byte_block_through_0x05ac8288. */
void bb_mutant_free_8_bytes_aligned_16(void *block)
{
    forward_sized_free(forward_allocator(), block, FORWARD_SMALL_SIZE, FORWARD_WRONG_ALIGNMENT);
}

/* Size 16: wrong for frees_8_byte_block_through_0x05ac8288. */
void bb_mutant_free_8_bytes_size_16(void *block)
{
    forward_sized_free(forward_allocator(), block, FORWARD_WRONG_SIZE, FORWARD_ALIGNMENT);
}

/* Passes the global's address instead of the allocator it holds: wrong for every forwarder. */
void bb_mutant_free_8_bytes_global_address(void *block)
{
    forward_sized_free(rt_guest(FORWARD_ALLOCATOR_GLOBAL), block, FORWARD_SMALL_SIZE, FORWARD_ALIGNMENT);
}

/* Alignment 16: wrong for frees_sized_block_aligned_8_through_0x05ac8288. */
void bb_mutant_free_sized_aligned_16(void *block, uint64_t size)
{
    forward_sized_free(forward_allocator(), block, size, FORWARD_WRONG_ALIGNMENT);
}

/* Ignores the size and frees 8 bytes: wrong for frees_sized_block_aligned_8_through_0x05ac8288. */
void bb_mutant_free_sized_fixed_size_8(void *block, uint64_t size)
{
    (void)size;
    forward_sized_free(forward_allocator(), block, FORWARD_SMALL_SIZE, FORWARD_ALIGNMENT);
}

/* Swaps the block and the size: wrong for frees_sized_block_aligned_8_through_0x05ac8288. */
void bb_mutant_free_sized_swapped(void *block, uint64_t size)
{
    forward_sized_free(forward_allocator(), (void *)(uintptr_t)size, (uint64_t)(uintptr_t)block, FORWARD_ALIGNMENT);
}

/* Alignment 16: wrong for allocates_aligned_8_through_0x05ac8288. */
void *bb_mutant_allocate_8_aligned_16(uint64_t size)
{
    return forward_allocate(forward_allocator(), size, FORWARD_WRONG_ALIGNMENT);
}

/* Allocates but answers null: wrong for both allocation forwarders. */
void *bb_mutant_allocate_8_result_dropped(uint64_t size)
{
    forward_allocate(forward_allocator(), size, FORWARD_ALIGNMENT);
    return 0;
}

/* Passes the global's address instead of the allocator it holds. */
void *bb_mutant_allocate_8_global_address(uint64_t size)
{
    return forward_allocate(rt_guest(FORWARD_ALLOCATOR_GLOBAL), size, FORWARD_ALIGNMENT);
}

/* Ignores the alignment given and uses 8: wrong for allocates_with_alignment_through_0x05ac8288. */
void *bb_mutant_allocate_fixed_alignment_8(uint64_t size, uint64_t alignment)
{
    (void)alignment;
    return forward_allocate(forward_allocator(), size, FORWARD_ALIGNMENT);
}

/* Swaps the size and the alignment: wrong for allocates_with_alignment_through_0x05ac8288. */
void *bb_mutant_allocate_swapped(uint64_t size, uint64_t alignment)
{
    return forward_allocate(forward_allocator(), alignment, size);
}

/* Allocates but answers null: wrong for allocates_with_alignment_through_0x05ac8288. */
void *bb_mutant_allocate_result_dropped(uint64_t size, uint64_t alignment)
{
    forward_allocate(forward_allocator(), size, alignment);
    return 0;
}

/* resets_with_0x02c375e0_and_returns_object: 0x02c375e0 with the object, then the object. */
#define RESET_OBJECT 0x02c375e0u
typedef void *reset_object_fn(void *object);

/* Answers what 0x02c375e0 left in rax instead of the object. */
void *bb_mutant_reset_returns_callee_result(void *object)
{
    return ((reset_object_fn *)rt_guest(RESET_OBJECT))(object);
}

/* Answers the object without resetting it. */
void *bb_mutant_reset_skipped(void *object)
{
    return object;
}

/* releases_value_at_0x8_with_0x005c14d0_then_clears_kind: the value at +0x8 released, then its
   first byte (the kind) cleared. */
#define RELEASE_VALUE 0x005c14d0u
struct release_holder {
    uint8_t unknown_0x00[0x8];
    uint8_t *value;
};
typedef void release_value_fn(uint8_t *value);

static void release_with(struct release_holder *holder, int reload, int clear_first, int clear)
{
    uint8_t *value = holder->value;
    if (clear_first)
        *value = 0;
    ((release_value_fn *)rt_guest(RELEASE_VALUE))(value);
    if (reload)
        value = *(uint8_t *volatile *)&holder->value;
    if (clear)
        *value = 0;
}

/* Control: reads +0x8 again after the release and clears the kind of what it finds there. */
void bb_mutant_release_value_read_again(struct release_holder *holder)
{
    release_with(holder, 1, 0, 1);
}

/* Clears the kind before the release. */
void bb_mutant_release_clears_first(struct release_holder *holder)
{
    release_with(holder, 0, 1, 0);
}

/* Leaves the kind as it was. */
void bb_mutant_release_keeps_kind(struct release_holder *holder)
{
    release_with(holder, 0, 0, 0);
}

/* equals_by_kind_and_16_bytes_at_0x10: slot 0 answers a kind (a byte); equal kinds, then memcmp
   of the 16 bytes +0x10 points at, a 32-bit answer. */
#define EQUAL_MEMCMP 0x02fbe638u
#define EQUAL_KEY_SIZE 16u
#define EQUAL_SHORT_KEY 8u
struct equal_object {
    void **vtable;
    void *unknown_0x08;
    const uint8_t *key;
};
typedef uint64_t equal_kind_fn(struct equal_object *);
typedef uint64_t equal_memcmp_fn(const void *, const void *, uint64_t);

/* What the mutant gets wrong. */
#define EQUAL_WHOLE_KIND 1  /* compares the kinds' whole registers */
#define EQUAL_KEYS_FIRST 2  /* reads both keys before the kind calls */
#define EQUAL_TABLE_FIRST 4  /* reads the second table before the first call */
#define EQUAL_WHOLE_ANSWER 8  /* tests memcmp's whole register */
#define EQUAL_SHORT 16  /* compares 8 bytes */
#define EQUAL_ALWAYS 32  /* calls memcmp even when the kinds differ */

static uint8_t equal_with(struct equal_object *object, struct equal_object *other, int wrong)
{
    const uint8_t *object_key = object->key;
    const uint8_t *other_key = other->key;
    void **other_table = other->vtable;
    uint64_t kind = ((equal_kind_fn *)object->vtable[0])(object);
    if (!(wrong & EQUAL_TABLE_FIRST))
        other_table = *(void **volatile *)&other->vtable;
    uint64_t other_kind = ((equal_kind_fn *)other_table[0])(other);
    int same = (wrong & EQUAL_WHOLE_KIND) ? kind == other_kind : (uint8_t)kind == (uint8_t)other_kind;
    if (!same && !(wrong & EQUAL_ALWAYS))
        return 0;
    if (!(wrong & EQUAL_KEYS_FIRST)) {
        object_key = *(const uint8_t *volatile *)&object->key;
        other_key = *(const uint8_t *volatile *)&other->key;
    }
    uint64_t answer = ((equal_memcmp_fn *)rt_guest(EQUAL_MEMCMP))(object_key, other_key,
                                                                 (wrong & EQUAL_SHORT) ? EQUAL_SHORT_KEY : EQUAL_KEY_SIZE);
    if (!same)
        return 0;
    return (wrong & EQUAL_WHOLE_ANSWER) ? answer == 0 : (uint32_t)answer == 0;
}

uint8_t bb_mutant_equal_whole_kind(struct equal_object *object, struct equal_object *other)
{
    return equal_with(object, other, EQUAL_WHOLE_KIND);
}

/* Control: both keys read before the kind calls. */
uint8_t bb_mutant_equal_keys_read_first(struct equal_object *object, struct equal_object *other)
{
    return equal_with(object, other, EQUAL_KEYS_FIRST);
}

/* Control: the second object's table read before the first kind call. */
uint8_t bb_mutant_equal_table_read_first(struct equal_object *object, struct equal_object *other)
{
    return equal_with(object, other, EQUAL_TABLE_FIRST);
}

uint8_t bb_mutant_equal_whole_answer(struct equal_object *object, struct equal_object *other)
{
    return equal_with(object, other, EQUAL_WHOLE_ANSWER);
}

uint8_t bb_mutant_equal_short_key(struct equal_object *object, struct equal_object *other)
{
    return equal_with(object, other, EQUAL_SHORT);
}

uint8_t bb_mutant_equal_always_compares(struct equal_object *object, struct equal_object *other)
{
    return equal_with(object, other, EQUAL_ALWAYS);
}
