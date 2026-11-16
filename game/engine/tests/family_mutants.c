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

/* The engine array (game/engine/container.h's Array): begin, end and capacity end after a first
   qword, then its allocator, whose vtable's allocate (slot 0x58) and free (slot 0x70) these
   call. */
struct mutant_array {
    uint64_t unknown_0x00;
    uint8_t *begin;
    uint8_t *end;
    uint8_t *capacity_end;
    void *allocator;
};
struct mutant_allocator_vtable {
    void *unknown_slots[11];
    void *(*allocate)(void *allocator, uint64_t size, uint64_t alignment);
    void *unknown_slots_1[2];
    void (*free)(void *allocator, void *data);
};

/* A reallocation without the failure report: the elements copied forwards (a null buffer skips
   the copy), the old buffer freed unless keep_old, the three pointers set. */
static void mutant_array_reserve(struct mutant_array *array, uint64_t capacity, uint64_t element,
                                 uint64_t alignment, int keep_old)
{
    const struct mutant_allocator_vtable *vtable = *(const struct mutant_allocator_vtable **)array->allocator;
    uint8_t *buffer = vtable->allocate(array->allocator, capacity * element, alignment);
    uint64_t count = (uint64_t)(array->end - array->begin) / element;
    if (buffer)
        for (uint64_t i = 0; i < count * element; i++)
            buffer[i] = array->begin[i];
    if (array->begin && !keep_old)
        vtable->free(array->allocator, array->begin);
    array->capacity_end = buffer + capacity * element;
    array->end = buffer + count * element;
    array->begin = buffer;
}

#define MUTANT_ELEMENT_8 8u
#define MUTANT_ELEMENT_4 4u
#define MUTANT_ELEMENT_12 12u
#define MUTANT_ELEMENT_16 16u
#define MUTANT_ALIGN_4 4u
#define MUTANT_ALIGN_8 8u

/* Reallocates as if the elements were 8 bytes: wrong for reallocates_array_of_4_byte_elements
   and reallocates_array_of_12_byte_elements (the size asked for, the copy and the pointers). */
void bb_mutant_array_reserve_8_byte_elements(struct mutant_array *array, uint64_t capacity)
{
    mutant_array_reserve(array, capacity, MUTANT_ELEMENT_8, MUTANT_ALIGN_8, 0);
}

/* Reallocates 4-byte elements but keeps the old buffer: wrong for
   reallocates_array_of_4_byte_elements, which frees it. */
void bb_mutant_array32_reserve_keeps_old_buffer(struct mutant_array *array, uint64_t capacity)
{
    mutant_array_reserve(array, capacity, MUTANT_ELEMENT_4, MUTANT_ALIGN_4, 1);
}

/* The same for reallocates_array_of_12_byte_elements. */
void bb_mutant_array96_reserve_keeps_old_buffer(struct mutant_array *array, uint64_t capacity)
{
    mutant_array_reserve(array, capacity, MUTANT_ELEMENT_12, MUTANT_ALIGN_4, 1);
}

/* Grows a full array of 16-byte elements by one element only: wrong for
   grows_full_array_of_16_byte_elements, which grows to one and a half times the count. */
void bb_mutant_array128_grows_by_one(struct mutant_array *array)
{
    uint64_t count = (uint64_t)(array->end - array->begin) / MUTANT_ELEMENT_16;
    if (array->end == array->capacity_end)
        mutant_array_reserve(array, count + 1, MUTANT_ELEMENT_16, MUTANT_ALIGN_8, 0);
}

/* Grows the array of 16-byte elements whether or not it is full: wrong for
   grows_full_array_of_16_byte_elements, which leaves an array with room alone. */
void bb_mutant_array128_grows_with_room(struct mutant_array *array)
{
    uint64_t count = (uint64_t)(array->end - array->begin) / MUTANT_ELEMENT_16;
    mutant_array_reserve(array, count + (count >> 1) + 1, MUTANT_ELEMENT_16, MUTANT_ALIGN_8, 0);
}

/* takes_and_erases_map_entry_at_0x3b0: the map at +0x3b0 of the object (its head at +0x3b8),
   the nodes of game/engine/msvc_tree.h keyed by 32-bit ids and holding counted objects (count at
   +0x8), and the map's erase by iterator, which takes the iterator by value. */
#define MUTANT_MAP_OFFSET 0x3b0u
#define MUTANT_MAP_ERASE 0x022a8a80u
#define MUTANT_OBJECT_REFERENCES 0x8u
struct mutant_map_node {
    struct mutant_map_node *left;
    struct mutant_map_node *parent;
    struct mutant_map_node *right;
    uint8_t color;
    uint8_t is_nil;
    uint8_t unknown_0x1a[6];
    uint32_t key;
    uint8_t unknown_0x24[4];
    uint8_t *object;
};
struct mutant_map {
    uint64_t unknown_0x00;
    struct mutant_map_node *head;
};
struct mutant_map_iterator {
    uint8_t unknown_0x00[0x10];
    struct mutant_map_node *node;
};
typedef struct mutant_map_iterator mutant_map_erase(struct mutant_map *, struct mutant_map_iterator);

/* The first node whose key is not below `key` (signed: compared as signed ids), when its key is
   `key`; else null. */
static struct mutant_map_node *mutant_map_find(struct mutant_map *map, uint32_t key, int is_signed)
{
    struct mutant_map_node *found = map->head;
    for (struct mutant_map_node *node = map->head->parent; !node->is_nil;) {
        int below = is_signed ? (int32_t)node->key < (int32_t)key : node->key < key;
        if (below) {
            node = node->right;
        } else {
            found = node;
            node = node->left;
        }
    }
    return found != map->head && found->key == key ? found : 0;
}

static uint8_t **mutant_map_take(uint8_t **out, uint8_t *owner, const uint32_t *key, int is_signed, int retains,
                                 int erases)
{
    struct mutant_map *map = (struct mutant_map *)(owner + MUTANT_MAP_OFFSET);
    struct mutant_map_node *node = mutant_map_find(map, *key, is_signed);
    *out = 0;
    if (!node)
        return out;
    if (node->object) {
        *out = node->object;
        if (retains)
            __atomic_fetch_add((int32_t *)(node->object + MUTANT_OBJECT_REFERENCES), 1, __ATOMIC_SEQ_CST);
    }
    if (erases) {
        struct mutant_map_iterator where = {{0}, node};
        ((mutant_map_erase *)rt_guest(MUTANT_MAP_ERASE))(map, where);
    }
    return out;
}

/* Takes the reference but leaves the entry in the map: wrong for
   takes_and_erases_map_entry_at_0x3b0, which erases it. */
uint8_t **bb_mutant_map_take_keeps_entry(uint8_t **out, uint8_t *owner, const uint32_t *key)
{
    return mutant_map_take(out, owner, key, 0, 1, 0);
}

/* Hands the object out without a new reference: wrong for takes_and_erases_map_entry_at_0x3b0. */
uint8_t **bb_mutant_map_take_without_reference(uint8_t **out, uint8_t *owner, const uint32_t *key)
{
    return mutant_map_take(out, owner, key, 0, 0, 1);
}

/* Orders the ids as signed: wrong for takes_and_erases_map_entry_at_0x3b0, whose ids are
   unsigned. */
uint8_t **bb_mutant_map_take_signed_ids(uint8_t **out, uint8_t *owner, const uint32_t *key)
{
    return mutant_map_take(out, owner, key, 1, 1, 1);
}

/* releases_slot_blocks_at_0x38: the slot table (count at +0x1c, ids at +0x28, second ids at
   +0x30, blocks at +0x38, the byte at +0x8) and the free the global 0x056c45d0 points at, which
   takes the block 8 bytes before the pointer a slot holds. Each control changes one read the
   cases show comes after a free. */
#define MUTANT_FREE_BLOCK_GLOBAL 0x056c45d0u
#define MUTANT_BLOCK_HEADER 0x8u
struct mutant_slot_table {
    uint8_t unknown_0x00[0x8];
    uint8_t released;
    uint8_t unknown_0x09[0x13];
    int32_t count;
    uint8_t unknown_0x20[0x8];
    uint32_t *ids;
    uint32_t *second_ids;
    uint8_t **blocks;
};
typedef void mutant_free_block(void *block);
enum mutant_slot_change { slot_as_game, slot_id_after_free, slot_count_once, slot_blocks_once };

static void mutant_slot_release(uint8_t *held)
{
    (*(mutant_free_block **)rt_guest(MUTANT_FREE_BLOCK_GLOBAL))(held - MUTANT_BLOCK_HEADER);
}

static void mutant_slot_blocks(struct mutant_slot_table *table, int keep, enum mutant_slot_change change)
{
    const int32_t count = table->count;
    uint8_t **const entry_blocks = table->blocks;
    if (keep) {
        for (int64_t slot = (int64_t)count - 1; slot >= 0; --slot) {
            uint32_t id = table->ids[slot];
            uint8_t *held = (change == slot_blocks_once ? entry_blocks : table->blocks)[slot];
            if (held)
                mutant_slot_release(held);
            if (change == slot_id_after_free)
                id = table->ids[slot];
            (change == slot_blocks_once ? entry_blocks : table->blocks)[slot] = 0;
            table->ids[slot] = id;
            table->second_ids[slot] = id;
        }
        return;
    }
    int32_t remaining = count;
    if (count > 0) {
        for (uint32_t slot = 0; slot != (uint32_t)count; ++slot) {
            uint8_t **blocks = change == slot_blocks_once ? entry_blocks : table->blocks;
            if (blocks[slot])
                mutant_slot_release(blocks[slot]);
            blocks = change == slot_blocks_once ? entry_blocks : table->blocks;
            int64_t from = (int64_t)count + slot;
            int32_t limit = change == slot_count_once ? count : table->count;
            if ((int32_t)(count + slot) < limit) {
                blocks[slot] = blocks[from];
                table->ids[slot] = table->ids[from];
            } else {
                blocks[slot] = 0;
            }
        }
        remaining = change == slot_count_once ? count : table->count;
    }
    table->count = remaining - count;
    table->released = 1;
}

/* Reads the slot's id again after its free (the game keeps the one read before). */
void bb_mutant_slot_blocks_id_after_free(struct mutant_slot_table *table, int keep)
{
    mutant_slot_blocks(table, keep, slot_id_after_free);
}

/* Takes the count once on entry (the game reads it again after each free). */
void bb_mutant_slot_blocks_count_once(struct mutant_slot_table *table, int keep)
{
    mutant_slot_blocks(table, keep, slot_count_once);
}

/* Takes the blocks array once on entry (the game reads it again after each free). */
void bb_mutant_slot_blocks_array_once(struct mutant_slot_table *table, int keep)
{
    mutant_slot_blocks(table, keep, slot_blocks_once);
}
