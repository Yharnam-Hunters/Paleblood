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

/* ---- variant property, wide string copy, pool appends, string thunk -------------------------
   Control mutants for game/engine/variant_string_pool_families.cpp: each changes one claim (a
   rounding, a skipped reference, or a value read before or after a call instead of where the
   original reads it), so the cases whose calls change that value must catch it. */

#define VSP_INVALID_UNREF_MESSAGE 0x04d40331u
#define VSP_INCOMPATIBLE_HEAP_MESSAGE 0x04b37dc0u
#define VSP_FATAL_ERROR 0x024b55b0u
#define VSP_THREAD_HEAP_ALLOCATOR 0x0247d7a0u
#define VSP_WIDE_STRING_ASSIGN 0x02a2a310u
#define VSP_ENGINE_MEMCPY 0x02a1a1b0u
#define VSP_POOL_MAP_ENTRY 0x02edc8e0u
#define VSP_MAKE_STRING 0x00671390u
#define VSP_RELEASE_NODE 0x004f0410u
#define VSP_ASSIGN_STRING 0x007183d0u

enum {
    VSP_REPORT_LINE = 0x3e,
    VSP_VARIANT_FLOAT = 1,
    VSP_VARIANT_INTEGER = 2,
    VSP_VARIANT_OBJECT = 3,
    VSP_PROPERTY_AT_0X168 = 0x3ea,
    VSP_PROPERTY_AT_0X16C = 0x3eb,
    VSP_WIDE_INLINE_CAPACITY = 8,
    VSP_HEAP_VALID = 0x20,
    VSP_ELEMENT_BYTES = 4,
    VSP_SELECTED_SLOT = 16,
    VSP_DEFAULT_SLOT = 5,
    VSP_OBJECT_INPUT = 0x40,
    VSP_CONTEXT_INPUT = 0x70,
};

typedef void (*vsp_fatal_error_fn)(const char *file, int32_t line, const char *format, ...);

static void vsp_report(uint32_t message)
{
    ((vsp_fatal_error_fn)rt_guest(VSP_FATAL_ERROR))(0, VSP_REPORT_LINE, (const char *)rt_guest(message));
}

/* -- the variant property setter -- */

struct vsp_variant {
    uint64_t value;
    int32_t type;
};

struct vsp_ref_object;
struct vsp_ref_table {
    void (*destroy)(struct vsp_ref_object *);
};
struct vsp_ref_object {
    const struct vsp_ref_table *vtable;
    int32_t references;
};

struct vsp_property_owner {
    uint8_t unknown_0x0[0x168];
    int32_t property_0x3ea;
    int32_t property_0x3eb;
};

/* The setter with a rounding conversion (round to nearest instead of truncation), or without the
   reference taken and dropped for an object variant. */
static void vsp_variant_property(struct vsp_property_owner *owner, int32_t property, const struct vsp_variant *variant,
                                 int round, int keep_reference)
{
    const int32_t type = variant->type;
    struct vsp_ref_object *object = (struct vsp_ref_object *)(uintptr_t)variant->value;
    if (keep_reference && type == VSP_VARIANT_OBJECT)
        __atomic_fetch_add(&object->references, 1, __ATOMIC_SEQ_CST);
    const uint32_t bits = (uint32_t)variant->value;
    float number;
    __builtin_memcpy(&number, &bits, sizeof number);
    int32_t value = 0;
    if (type == VSP_VARIANT_FLOAT)
        value = round ? (int32_t)__builtin_lrintf(number) : (int32_t)number;
    else if (type == VSP_VARIANT_INTEGER)
        value = (int32_t)bits;
    if (property == VSP_PROPERTY_AT_0X16C)
        owner->property_0x3eb = value;
    else if (property == VSP_PROPERTY_AT_0X168)
        owner->property_0x3ea = value;
    if (keep_reference && type == VSP_VARIANT_OBJECT) {
        const int32_t before = __atomic_fetch_sub(&object->references, 1, __ATOMIC_SEQ_CST);
        if (before == 1)
            object->vtable->destroy(object);
        else if (before <= 0)
            vsp_report(VSP_INVALID_UNREF_MESSAGE);
    }
}

void bb_mutant_variant_property_rounds(struct vsp_property_owner *owner, int32_t property,
                                       const struct vsp_variant *variant)
{
    vsp_variant_property(owner, property, variant, 1, 1);
}

void bb_mutant_variant_property_skips_reference(struct vsp_property_owner *owner, int32_t property,
                                                const struct vsp_variant *variant)
{
    vsp_variant_property(owner, property, variant, 0, 0);
}

/* -- the wide string copy -- */

struct vsp_wide_string {
    uint64_t unknown_0x00;
    union {
        uint16_t inline_text[VSP_WIDE_INLINE_CAPACITY];
        const uint16_t *heap;
    };
    uint64_t size;
    uint64_t capacity;
    void *allocator;
    uint8_t owns;
};

struct vsp_string_holder {
    uint8_t unknown_0x0[0x28];
    struct vsp_wide_string text;
};

struct vsp_allocator_info {
    uint8_t flags;
    uint8_t unknown_0x1[0xf];
};

struct vsp_allocator;
struct vsp_allocator_table {
    void *unknown_slots[4];
    void (*info)(struct vsp_allocator_info *out, struct vsp_allocator *allocator, int32_t);
};
struct vsp_allocator {
    const struct vsp_allocator_table *vtable;
};

typedef struct vsp_allocator *(*vsp_thread_heap_fn)(void);
typedef void (*vsp_assign_fn)(struct vsp_wide_string *, const uint16_t *, uint64_t);

static const uint16_t *vsp_characters(const struct vsp_wide_string *text)
{
    return text->capacity >= VSP_WIDE_INLINE_CAPACITY ? text->heap : text->inline_text;
}

/* The copy reading the text and size after the heap check, or the owns flag before the
   assignment. */
static struct vsp_wide_string *vsp_wide_string_copy(struct vsp_wide_string *copy, const struct vsp_string_holder *source,
                                                    int text_late, int owns_early)
{
    const uint16_t *characters = vsp_characters(&source->text);
    uint64_t size = source->text.size;
    struct vsp_allocator *heap = ((vsp_thread_heap_fn)rt_guest(VSP_THREAD_HEAP_ALLOCATOR))();
    struct vsp_allocator_info info = {0};
    heap->vtable->info(&info, heap, 0);
    if (!(info.flags & VSP_HEAP_VALID))
        vsp_report(VSP_INCOMPATIBLE_HEAP_MESSAGE);
    if (text_late) {
        characters = vsp_characters(&source->text);
        size = source->text.size;
    }
    copy->allocator = heap;
    copy->capacity = VSP_WIDE_INLINE_CAPACITY - 1;
    copy->size = 0;
    copy->inline_text[0] = 0;
    const uint8_t owns = source->text.owns;
    ((vsp_assign_fn)rt_guest(VSP_WIDE_STRING_ASSIGN))(copy, characters, size);
    copy->owns = owns_early ? owns : source->text.owns;
    return copy;
}

struct vsp_wide_string *bb_mutant_wide_string_copy_reads_text_late(struct vsp_wide_string *copy,
                                                                   const struct vsp_string_holder *source)
{
    return vsp_wide_string_copy(copy, source, 1, 0);
}

struct vsp_wide_string *bb_mutant_wide_string_copy_reads_owns_early(struct vsp_wide_string *copy,
                                                                    const struct vsp_string_holder *source)
{
    return vsp_wide_string_copy(copy, source, 0, 1);
}

/* -- the pool appends -- */

struct vsp_block_pair {
    uint8_t unknown_0x0[0x8];
    const void *first;
    const void *second;
    uint32_t count;
};

struct vsp_pool {
    void *map;
    uint8_t *bases[VSP_SELECTED_SLOT + 1];
    uint8_t unknown_0x90[0x28];
    uint32_t used[VSP_SELECTED_SLOT + 1];
};

typedef void (*vsp_memcpy_fn)(void *, const void *, int64_t);
typedef uint8_t **(*vsp_map_entry_fn)(void *, const void *const *);

/* The append with the used size moved on from the value read before the copy. */
static void vsp_pool_append(struct vsp_pool *pool, uint32_t slot, const void *block, uint32_t count, int used_snapshot)
{
    const uint32_t bytes = count * VSP_ELEMENT_BYTES;
    const uint32_t before = pool->used[slot];
    uint8_t *destination = pool->bases[slot] + before;
    ((vsp_memcpy_fn)rt_guest(VSP_ENGINE_MEMCPY))(destination, block, bytes);
    pool->used[slot] = used_snapshot ? before + bytes : pool->used[slot] + bytes;
    const void *key = block;
    *((vsp_map_entry_fn)rt_guest(VSP_POOL_MAP_ENTRY))(pool->map, &key) = destination;
}

/* The appends with the first count kept for the second block, the used size from a snapshot, or
   the second block's address read before the first append. */
static uint8_t vsp_pool_appends(struct vsp_pool *pool, const struct vsp_block_pair *pair, uint64_t select,
                                int count_snapshot, int used_snapshot, int second_early)
{
    const uint32_t slot = (uint8_t)select ? VSP_SELECTED_SLOT : VSP_DEFAULT_SLOT;
    const void *first = pair->first;
    const void *second = pair->second;
    uint32_t count = pair->count;
    if (first) {
        vsp_pool_append(pool, slot, first, count, used_snapshot);
        if (!count_snapshot)
            count = pair->count;
    }
    if (!second_early)
        second = pair->second;
    if (second)
        vsp_pool_append(pool, slot, second, count, used_snapshot);
    return 1;
}

uint8_t bb_mutant_pool_appends_keep_first_count(void *unused, struct vsp_pool *pool, const struct vsp_block_pair *pair,
                                                uint64_t select)
{
    (void)unused;
    return vsp_pool_appends(pool, pair, select, 1, 0, 0);
}

uint8_t bb_mutant_pool_appends_used_from_snapshot(void *unused, struct vsp_pool *pool, const struct vsp_block_pair *pair,
                                                  uint64_t select)
{
    (void)unused;
    return vsp_pool_appends(pool, pair, select, 0, 1, 0);
}

uint8_t bb_mutant_pool_appends_second_read_early(void *unused, struct vsp_pool *pool, const struct vsp_block_pair *pair,
                                                 uint64_t select)
{
    (void)unused;
    return vsp_pool_appends(pool, pair, select, 0, 0, 1);
}

/* -- the string thunk -- */

struct vsp_node {
    uint8_t unknown_0x0[0x18];
    int32_t references;
};
struct vsp_manager {
    uint8_t unknown_0x0[0x48];
    struct vsp_node empty;
};
struct vsp_vm_owner {
    uint8_t unknown_0x0[0x218];
    struct vsp_manager *strings;
};
struct vsp_vm {
    uint8_t unknown_0x0[0x10];
    struct vsp_vm_owner *owner;
    uint8_t unknown_0x18[0x88];
    uint8_t exception;
};
struct vsp_maker_info {
    uint8_t unknown_0x0[0x10];
    uint8_t *context;
};
struct vsp_maker_link {
    uint8_t unknown_0x0[0x80];
    struct vsp_maker_info *info;
};
struct vsp_argument_object {
    uint8_t unknown_0x0[0x28];
    struct vsp_maker_link *link;
};
struct vsp_arguments {
    uint8_t unknown_0x0[0x10];
    struct vsp_argument_object *object;
};

typedef void (*vsp_make_string_fn)(struct vsp_node **, void *, void *);
typedef void (*vsp_release_node_fn)(struct vsp_node *);
typedef void (*vsp_assign_string_fn)(void *, struct vsp_node *const *);

static void vsp_drop(struct vsp_node *node)
{
    if (--node->references == 0)
        ((vsp_release_node_fn)rt_guest(VSP_RELEASE_NODE))(node);
}

/* The thunk reading the exception flag before the drops, or dropping the string it held before
   the assignment rather than the one the assignment left. */
static void vsp_string_thunk(struct vsp_vm *vm, const struct vsp_arguments *arguments, void *result,
                             int exception_early, int held_snapshot)
{
    struct vsp_argument_object *object = arguments->object;
    struct vsp_node *empty = &vm->owner->strings->empty;
    struct vsp_node *held = empty;
    ++held->references;
    uint8_t *context = object->link->info->context;
    struct vsp_node *made = 0;
    ((vsp_make_string_fn)rt_guest(VSP_MAKE_STRING))(&made, (uint8_t *)object + VSP_OBJECT_INPUT,
                                                    context + VSP_CONTEXT_INPUT);
    struct vsp_node *created = made;
    const uint8_t early = vm->exception;
    ++created->references;
    vsp_drop(empty);
    held = created;
    vsp_drop(created);
    if (!(exception_early ? early : vm->exception))
        ((vsp_assign_string_fn)rt_guest(VSP_ASSIGN_STRING))(result, &held);
    vsp_drop(held_snapshot ? created : held);
}

void bb_mutant_string_thunk_reads_exception_early(void *unused, struct vsp_vm *vm, const struct vsp_arguments *arguments,
                                                  void *result)
{
    (void)unused;
    vsp_string_thunk(vm, arguments, result, 1, 0);
}

void bb_mutant_string_thunk_drops_snapshot(void *unused, struct vsp_vm *vm, const struct vsp_arguments *arguments,
                                           void *result)
{
    (void)unused;
    vsp_string_thunk(vm, arguments, result, 0, 1);
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

/* The eight destroys_with_0x..._then_deletes families (game/engine/destroy_then_delete_families.cpp):
   the family's destructor, then operator delete (0x02fbe588), with the object. */
#define DELETING_DTOR_OPERATOR_DELETE 0x02fbe588u
typedef void mutant_destroyed_fn(void *object);

static void mutant_call_on_object(uint32_t function, void *object)
{
    ((mutant_destroyed_fn *)rt_guest(function))(object);
}

/* Deletes without destroying: wrong for every destroys_with_0x..._then_deletes family. */
void bb_mutant_deleting_dtor_skips_destructor(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
}

/* Per family: destroys without deleting, and deletes before destroying. */
#define DELETING_DTOR_0x010f6190 0x010f6190u
void bb_mutant_deleting_dtor_0x010f6190_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x010f6190, object);
}
void bb_mutant_deleting_dtor_0x010f6190_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x010f6190, object);
}

#define DELETING_DTOR_0x02af2b00 0x02af2b00u
void bb_mutant_deleting_dtor_0x02af2b00_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af2b00, object);
}
void bb_mutant_deleting_dtor_0x02af2b00_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af2b00, object);
}

#define DELETING_DTOR_0x02af4240 0x02af4240u
void bb_mutant_deleting_dtor_0x02af4240_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af4240, object);
}
void bb_mutant_deleting_dtor_0x02af4240_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af4240, object);
}

#define DELETING_DTOR_0x02af6d40 0x02af6d40u
void bb_mutant_deleting_dtor_0x02af6d40_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af6d40, object);
}
void bb_mutant_deleting_dtor_0x02af6d40_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af6d40, object);
}

#define DELETING_DTOR_0x02af8220 0x02af8220u
void bb_mutant_deleting_dtor_0x02af8220_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af8220, object);
}
void bb_mutant_deleting_dtor_0x02af8220_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af8220, object);
}

#define DELETING_DTOR_0x02af9940 0x02af9940u
void bb_mutant_deleting_dtor_0x02af9940_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af9940, object);
}
void bb_mutant_deleting_dtor_0x02af9940_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af9940, object);
}

#define DELETING_DTOR_0x02afc240 0x02afc240u
void bb_mutant_deleting_dtor_0x02afc240_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02afc240, object);
}
void bb_mutant_deleting_dtor_0x02afc240_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02afc240, object);
}

#define DELETING_DTOR_0x02afee40 0x02afee40u
void bb_mutant_deleting_dtor_0x02afee40_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02afee40, object);
}
void bb_mutant_deleting_dtor_0x02afee40_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02afee40, object);
}

/* Mutants from #1170. */

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

/* Mutants from #1171. */

/* The engine array (game/engine/container.h's Array) as array_resize_families.cpp's reallocations
   see it: begin, end and capacity end after a first qword, then its allocator, whose vtable's
   allocate (slot 0x58) and free (slot 0x70) they call. */
struct mutant_resize_array {
    uint64_t unknown_0x00;
    uint8_t *begin;
    uint8_t *end;
    uint8_t *capacity_end;
    void *allocator;
};
struct mutant_resize_allocator_vtable {
    void *unknown_slots[11];
    void *(*allocate)(void *allocator, uint64_t size, uint64_t alignment);
    void *unknown_slots_1[2];
    void (*free)(void *allocator, void *data);
};

/* A reallocation without the failure report: the elements copied forwards (a null buffer skips
   the copy), the old buffer freed unless keep_old, the three pointers set. */
static void mutant_resize_reserve(struct mutant_resize_array *array, uint64_t capacity, uint64_t element,
                                  uint64_t alignment, int keep_old)
{
    const struct mutant_resize_allocator_vtable *vtable =
        *(const struct mutant_resize_allocator_vtable **)array->allocator;
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

#define MUTANT_RESIZE_ELEMENT_8 8u
#define MUTANT_RESIZE_ELEMENT_16 16u
#define MUTANT_RESIZE_ALIGN_4 4u
#define MUTANT_RESIZE_ALIGN_8 8u

/* Reallocates 8-byte elements asking for 8-byte alignment: wrong for
   reallocates_array_of_8_byte_4_aligned_elements, which asks for 4. */
void bb_mutant_array64_reserve_8_aligned(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_8, MUTANT_RESIZE_ALIGN_8, 0);
}

/* Reallocates 4-aligned 8-byte elements but keeps the old buffer: wrong for
   reallocates_array_of_8_byte_4_aligned_elements, which frees it. */
void bb_mutant_array64_4_aligned_reserve_keeps_old_buffer(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_8, MUTANT_RESIZE_ALIGN_4, 1);
}

/* Reallocates as if the elements were 8 bytes: wrong for reallocates_array_of_16_byte_elements
   (the size asked for, the copy and the pointers). */
void bb_mutant_array128_reserve_as_8_byte_elements(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_8, MUTANT_RESIZE_ALIGN_8, 0);
}

/* Reallocates 16-byte elements but keeps the old buffer: wrong for
   reallocates_array_of_16_byte_elements, which frees it. */
void bb_mutant_array128_reserve_keeps_old_buffer(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_resize_reserve(array, capacity, MUTANT_RESIZE_ELEMENT_16, MUTANT_RESIZE_ALIGN_8, 1);
}

/* The array resizes_heap_array_of_8_byte_elements resizes (data, count, capacity) and the heap
   the global at 0x057e7320 points at, whose vtable reallocates (slot 0x60), frees (0x68) and
   allocates (0x70, with an owner and a pointer to a dword). */
#define MUTANT_ARRAY_HEAP 0x057e7320u
#define MUTANT_HEAP_ALLOCATION_TAG 2u
struct mutant_heap_array {
    uint64_t *data;
    uint64_t count;
    uint64_t capacity;
};
struct mutant_heap_vtable {
    void *unknown_slots[12];
    void *(*reallocate)(void *heap, void *data, uint64_t size);
    void (*free)(void *heap, void *data);
    void *(*allocate)(void *heap, const void *owner, uint64_t size, const uint32_t *tag);
};

/* The capacity change: zero frees, any other is rounded up to a multiple of granule. */
static void mutant_heap_array_capacity(struct mutant_heap_array *array, const void *owner, uint64_t capacity,
                                       uint64_t granule)
{
    void *heap = *(void **)rt_guest(MUTANT_ARRAY_HEAP);
    const struct mutant_heap_vtable *vtable = *(const struct mutant_heap_vtable **)heap;
    if (capacity == 0) {
        if (array->data) {
            vtable->free(heap, array->data);
            array->data = 0;
        }
        array->capacity = 0;
        return;
    }
    capacity = (capacity + granule - 1) / granule * granule;
    if (array->data) {
        array->data = vtable->reallocate(heap, array->data, capacity * sizeof(uint64_t));
    } else {
        const uint32_t tag = MUTANT_HEAP_ALLOCATION_TAG;
        array->data = vtable->allocate(heap, owner, capacity * sizeof(uint64_t), &tag);
    }
    array->capacity = capacity;
}

#define MUTANT_HEAP_GRANULE 4u
#define MUTANT_HEAP_WRONG_GRANULE 8u
#define MUTANT_HEAP_GROWTH_DIVISOR 4u
#define MUTANT_HEAP_SHRINK_DIVISOR 2u

/* The resize with the growth, the shrink, the granule and the owner as given. */
static void mutant_heap_array_resize(struct mutant_heap_array *array, const void *owner, uint64_t count,
                                     int grow_by_quarter, int shrink, uint64_t granule)
{
    const uint64_t capacity = array->capacity;
    if (count < array->count) {
        if (shrink && capacity / MUTANT_HEAP_SHRINK_DIVISOR > count)
            mutant_heap_array_capacity(array, owner, count, granule);
    } else if (capacity < count) {
        const uint64_t grown = grow_by_quarter ? count + count / MUTANT_HEAP_GROWTH_DIVISOR : count;
        if (capacity != grown)
            mutant_heap_array_capacity(array, owner, grown, granule);
    }
    array->count = count;
}

/* Grows to the count itself: wrong for resizes_heap_array_of_8_byte_elements, which grows to a
   quarter more. */
void bb_mutant_heap_array_grows_to_count(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    mutant_heap_array_resize(array, owner, count, 0, 1, MUTANT_HEAP_GRANULE);
}

/* Never shrinks the buffer: wrong for resizes_heap_array_of_8_byte_elements, which shrinks it
   below half the capacity. */
void bb_mutant_heap_array_never_shrinks(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    mutant_heap_array_resize(array, owner, count, 1, 0, MUTANT_HEAP_GRANULE);
}

/* Rounds capacities up to eight elements: wrong for resizes_heap_array_of_8_byte_elements, which
   rounds to four. */
void bb_mutant_heap_array_rounds_to_8(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    mutant_heap_array_resize(array, owner, count, 1, 1, MUTANT_HEAP_WRONG_GRANULE);
}

/* Allocates for the array instead of the owner it is given: wrong for
   resizes_heap_array_of_8_byte_elements, which passes its second argument on. */
void bb_mutant_heap_array_allocates_for_array(struct mutant_heap_array *array, const void *owner, uint64_t count)
{
    (void)owner;
    mutant_heap_array_resize(array, array, count, 1, 1, MUTANT_HEAP_GRANULE);
}

/* The handle records reallocates_array_of_24_byte_handle_records moves: 24 bytes, a handle that
   0x02fa7870 copy-constructs and 0x02fa7940 destroys, then a dword at +0x10. */
#define MUTANT_HANDLE_COPY 0x02fa7870u
#define MUTANT_HANDLE_DESTROY 0x02fa7940u
#define MUTANT_HANDLE_RECORD 24u
#define MUTANT_HANDLE_RECORD_VALUE 0x10u
#define MUTANT_HANDLE_RECORD_ALIGN 8u
typedef void mutant_handle_copy_fn(void *to, const void *from);
typedef void mutant_handle_destroy_fn(void *handle);

/* The reallocation without the failure report, with the reads as given: reload_copy_end reads
   the end before every copy, snapshot keeps the begin and end read before the copies, free_early
   frees with the begin and allocator read before the destruction, destroy whether the old records
   are destroyed. */
static void mutant_handle_records(struct mutant_resize_array *array, uint64_t capacity, int reload_copy_end,
                                  int snapshot, int free_early, int destroy)
{
    mutant_handle_copy_fn *copy = (mutant_handle_copy_fn *)rt_guest(MUTANT_HANDLE_COPY);
    mutant_handle_destroy_fn *destroy_handle = (mutant_handle_destroy_fn *)rt_guest(MUTANT_HANDLE_DESTROY);
    const struct mutant_resize_allocator_vtable *vtable =
        *(const struct mutant_resize_allocator_vtable **)array->allocator;
    uint8_t *buffer = vtable->allocate(array->allocator, capacity * MUTANT_HANDLE_RECORD, MUTANT_HANDLE_RECORD_ALIGN);
    uint8_t *first = array->begin;
    uint8_t *last = array->end;
    uint8_t *to = buffer;
    for (uint8_t *from = first; from != (reload_copy_end ? array->end : last); from += MUTANT_HANDLE_RECORD) {
        if (to) {
            copy(to, from);
            *(uint32_t *)(to + MUTANT_HANDLE_RECORD_VALUE) = *(const uint32_t *)(from + MUTANT_HANDLE_RECORD_VALUE);
        }
        to += MUTANT_HANDLE_RECORD;
    }
    uint8_t *begin = snapshot ? first : array->begin;
    uint8_t *end = snapshot ? last : array->end;
    void *allocator = array->allocator;
    if (begin) {
        if (destroy)
            for (uint8_t *record = begin; record != end; record += MUTANT_HANDLE_RECORD)
                destroy_handle(record);
        const struct mutant_resize_allocator_vtable *free_vtable;
        if (free_early) {
            free_vtable = *(const struct mutant_resize_allocator_vtable **)allocator;
            free_vtable->free(allocator, begin);
        } else {
            free_vtable = *(const struct mutant_resize_allocator_vtable **)array->allocator;
            free_vtable->free(array->allocator, array->begin);
        }
    }
    uint64_t count = (uint64_t)(end - begin) / MUTANT_HANDLE_RECORD;
    array->capacity_end = buffer + capacity * MUTANT_HANDLE_RECORD;
    array->end = buffer + count * MUTANT_HANDLE_RECORD;
    array->begin = buffer;
}

/* Keeps the begin and end read before the copies: wrong for
   reallocates_array_of_24_byte_handle_records, which reads them again after the copies. */
void bb_mutant_handle_records_snapshot_ends(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 0, 1, 0, 1);
}

/* Reads the end before every copy: wrong for reallocates_array_of_24_byte_handle_records, which
   copies up to the end read before the first. */
void bb_mutant_handle_records_reload_copy_end(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 1, 0, 0, 1);
}

/* Frees with the begin and allocator read before the destruction: wrong for
   reallocates_array_of_24_byte_handle_records, which reads both again after it. */
void bb_mutant_handle_records_free_early(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 0, 0, 1, 1);
}

/* Frees the old buffer without destroying its records: wrong for
   reallocates_array_of_24_byte_handle_records. */
void bb_mutant_handle_records_without_destroy(struct mutant_resize_array *array, uint64_t capacity)
{
    mutant_handle_records(array, capacity, 0, 0, 0, 0);
}

#define MUTANT_HANDLE_ASSIGN 0x02fa7bc0u
typedef void mutant_handle_assign_fn(void *to, const void *from);
/* The iterator erases_range_of_24_byte_handle_records takes by value and returns: the record
   pointer at +0x10. */
struct mutant_record_iterator {
    uint64_t unknown_0x0;
    uint64_t unknown_0x8;
    uint8_t *record;
};

/* The range erase with the reads as given: reload_assign_end reads the end before every
   assignment, snapshot_end destroys up to the end read before the assignments, whole_snapshot
   sets a wholly erased array's end to the begin read first, destroy whether records are
   destroyed. */
static struct mutant_record_iterator *mutant_erase_records(struct mutant_record_iterator *result,
                                                           struct mutant_resize_array *array,
                                                           const struct mutant_record_iterator *first,
                                                           const struct mutant_record_iterator *last,
                                                           int reload_assign_end, int snapshot_end, int whole_snapshot,
                                                           int destroy)
{
    mutant_handle_assign_fn *assign = (mutant_handle_assign_fn *)rt_guest(MUTANT_HANDLE_ASSIGN);
    mutant_handle_destroy_fn *destroy_handle = (mutant_handle_destroy_fn *)rt_guest(MUTANT_HANDLE_DESTROY);
    uint8_t *begin = array->begin;
    if (first->record == begin && last->record == array->end) {
        uint8_t *new_end = begin;
        if (begin != last->record) {
            if (destroy)
                for (uint8_t *record = begin; record != last->record; record += MUTANT_HANDLE_RECORD)
                    destroy_handle(record);
            new_end = whole_snapshot ? begin : array->begin;
        }
        array->end = new_end;
        result->record = begin;
        return result;
    }
    if (first->record != last->record) {
        uint8_t *end = array->end;
        uint8_t *kept_end = first->record;
        uint8_t *destroy_end = last->record;
        if (last->record != end) {
            for (uint8_t *from = last->record; from != (reload_assign_end ? array->end : end);
                 from += MUTANT_HANDLE_RECORD, kept_end += MUTANT_HANDLE_RECORD) {
                assign(kept_end, from);
                *(uint32_t *)(kept_end + MUTANT_HANDLE_RECORD_VALUE) = *(const uint32_t *)(from + MUTANT_HANDLE_RECORD_VALUE);
            }
            destroy_end = snapshot_end ? end : array->end;
        }
        if (destroy)
            for (uint8_t *record = kept_end; record != destroy_end; record += MUTANT_HANDLE_RECORD)
                destroy_handle(record);
        array->end = kept_end;
    }
    result->record = first->record;
    return result;
}

/* Reads the end before every assignment: wrong for erases_range_of_24_byte_handle_records,
   which moves the records up to the end read before the first. */
struct mutant_record_iterator *bb_mutant_erase_records_reload_assign_end(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 1, 0, 0, 1);
}

/* Destroys up to the end read before the assignments: wrong for
   erases_range_of_24_byte_handle_records, which reads it again after them. */
struct mutant_record_iterator *bb_mutant_erase_records_snapshot_end(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 0, 1, 0, 1);
}

/* Sets a wholly erased array's end to the begin read first: wrong for
   erases_range_of_24_byte_handle_records, which reads the begin again after the destruction. */
struct mutant_record_iterator *bb_mutant_erase_records_whole_snapshot_begin(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 0, 0, 1, 1);
}

/* Moves the records but destroys none: wrong for erases_range_of_24_byte_handle_records. */
struct mutant_record_iterator *bb_mutant_erase_records_without_destroy(
    struct mutant_record_iterator *result, struct mutant_resize_array *array, struct mutant_record_iterator first,
    struct mutant_record_iterator last)
{
    return mutant_erase_records(result, array, &first, &last, 0, 0, 0, 0);
}

/* Mutants from #1172. */

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

/* Mutants from #1175. */

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

/* Mutants from #1176. */

/* frees_kind_sized_dword_storage_through_0x05ac8288 (game/engine/kind_sized_dword_free_family.cpp):
   an object with a vtable whose slot 0x88 reports the kind, a buffer at +0x8 and the count at
   +0x18. Each mutant below gets one detail wrong; the controls move one read across the kind
   calls. */
#define DWORD_FREE_SIZED_FREE 0x02fa1c20u
#define DWORD_FREE_ALLOCATOR 0x05ac8288u
#define DWORD_FREE_KIND_SLOT 17
#define DWORD_FREE_THREE_PER_ENTRY 4u
#define DWORD_FREE_THREE_PER_ENTRY_AND_TWO 6u
#define DWORD_FREE_PER_ENTRY 3u
#define DWORD_FREE_EXTRA 2u
#define DWORD_FREE_SIZE 4u

struct dword_storage;
typedef int32_t (*dword_storage_kind)(struct dword_storage *, int32_t);
typedef void (*dword_storage_sized_free)(void *, void *, uint64_t, uint64_t);

struct dword_storage {
    void **vtable;
    void *storage;
    void *unknown_0x10;
    int32_t count;
};

/* What the mutant gets wrong. */
#define DWORD_FREE_ALLOCATOR_LATE 1  /* reads the allocator after the kind calls */
#define DWORD_FREE_COUNT_EARLY 2  /* reads the count before the kind calls */
#define DWORD_FREE_ASKS_ONCE 4  /* uses the first answer for the kind 6 test too */
#define DWORD_FREE_CACHES_VTABLE 8  /* calls the second time through the first vtable */
#define DWORD_FREE_NO_ENTRIES 16  /* leaves the entries out of the size */

static void *dword_storage_allocator(void)
{
    return *(void *volatile *)rt_guest(DWORD_FREE_ALLOCATOR);
}

static uint32_t dword_storage_ask(struct dword_storage *object, void **vtable)
{
    return (uint32_t)((dword_storage_kind)vtable[DWORD_FREE_KIND_SLOT])(object, 0) & ~1u;
}

static void dword_storage_free_with(struct dword_storage *object, int wrong)
{
    volatile int32_t *field = &object->count;
    void *allocator = dword_storage_allocator();
    int32_t early = *field;
    void **vtable = object->vtable;
    int32_t count;
    int32_t elements;
    uint32_t first = dword_storage_ask(object, vtable);
    if (first == DWORD_FREE_THREE_PER_ENTRY) {
        count = (wrong & DWORD_FREE_COUNT_EARLY) ? early : *field;
        elements = (int32_t)((uint32_t)count * DWORD_FREE_PER_ENTRY);
    } else {
        uint32_t second = dword_storage_ask(object, (wrong & DWORD_FREE_CACHES_VTABLE) ? vtable : object->vtable);
        if (wrong & DWORD_FREE_ASKS_ONCE)
            second = first;
        count = (wrong & DWORD_FREE_COUNT_EARLY) ? early : *field;
        elements = second == DWORD_FREE_THREE_PER_ENTRY_AND_TWO
                       ? (int32_t)((uint32_t)count * DWORD_FREE_PER_ENTRY + DWORD_FREE_EXTRA) : count;
    }
    if (wrong & DWORD_FREE_ALLOCATOR_LATE)
        allocator = dword_storage_allocator();
    int64_t dwords = (int64_t)elements + ((wrong & DWORD_FREE_NO_ENTRIES) ? 0 : (int64_t)count);
    ((dword_storage_sized_free)rt_guest(DWORD_FREE_SIZED_FREE))(allocator, object->storage,
                                                               (uint64_t)dwords * DWORD_FREE_SIZE, DWORD_FREE_SIZE);
}

void bb_mutant_dword_storage_free_reads_allocator_late(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_ALLOCATOR_LATE);
}

void bb_mutant_dword_storage_free_reads_count_early(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_COUNT_EARLY);
}

void bb_mutant_dword_storage_free_asks_once(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_ASKS_ONCE);
}

void bb_mutant_dword_storage_free_caches_vtable(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_CACHES_VTABLE);
}

void bb_mutant_dword_storage_free_without_entries(struct dword_storage *object)
{
    dword_storage_free_with(object, DWORD_FREE_NO_ENTRIES);
}

/* deletes_work_holder (game/engine/work_holder_delete_family.cpp): a holder (vtable, work at +0x8)
   of shared work (reference count +0x8, signal +0x10, users +0x30). */
#define WORK_HOLDER_BASE_VTABLE 0x057b1a80u
#define WORK_HOLDER_SIGNAL_SET 0x0247e780u
#define WORK_HOLDER_FATAL_ERROR 0x024b55b0u
#define WORK_HOLDER_DELETE 0x02fbe588u
#define WORK_HOLDER_INVALID_UNREF 0x04d40331u
#define WORK_HOLDER_UNREF_LINE 0x3e

struct mutant_work {
    void (**vtable)(struct mutant_work *);
    int32_t references;
    int32_t unknown_0x0c;
    uint8_t unknown_0x10[0x20];  /* the signal */
    int32_t users;
};
struct mutant_work_holder {
    const void *vtable;
    struct mutant_work *work;
};
typedef uint64_t (*work_holder_signal_set)(void *, uint8_t);
typedef void (*work_holder_fatal_error)(const char *, int32_t, const char *, ...);
typedef void (*work_holder_delete)(void *);

#define WORK_HOLDER_RIGHT 0
#define WORK_HOLDER_WORK_ONCE 1  /* does not read the work again after the signal */
#define WORK_HOLDER_SIGNAL_ON_ONE 2  /* sets the signal only when the users were exactly 1 */
#define WORK_HOLDER_KEPT 4  /* does not delete the holder */

static void work_holder_delete_with(struct mutant_work_holder *holder, int wrong)
{
    holder->vtable = rt_guest(WORK_HOLDER_BASE_VTABLE);
    struct mutant_work *work = holder->work;
    if (work) {
        int32_t users = __atomic_fetch_sub(&work->users, 1, __ATOMIC_SEQ_CST);
        if ((wrong & WORK_HOLDER_SIGNAL_ON_ONE) ? users == 1 : users <= 1)
            ((work_holder_signal_set)rt_guest(WORK_HOLDER_SIGNAL_SET))(work->unknown_0x10, 1);
        if (!(wrong & WORK_HOLDER_WORK_ONCE))
            work = *(struct mutant_work *volatile *)&holder->work;
        if (work) {
            int32_t references = __atomic_fetch_sub(&work->references, 1, __ATOMIC_SEQ_CST);
            if (references == 1)
                work->vtable[0](work);
            else if (references <= 0)
                ((work_holder_fatal_error)rt_guest(WORK_HOLDER_FATAL_ERROR))(
                    0, WORK_HOLDER_UNREF_LINE, (const char *)rt_guest(WORK_HOLDER_INVALID_UNREF));
        }
    }
    if (!(wrong & WORK_HOLDER_KEPT))
        ((work_holder_delete)rt_guest(WORK_HOLDER_DELETE))(holder);
}

void bb_mutant_work_holder_delete_work_once(struct mutant_work_holder *holder)
{
    work_holder_delete_with(holder, WORK_HOLDER_WORK_ONCE);
}

void bb_mutant_work_holder_delete_signal_on_one(struct mutant_work_holder *holder)
{
    work_holder_delete_with(holder, WORK_HOLDER_SIGNAL_ON_ONE);
}

void bb_mutant_work_holder_delete_kept(struct mutant_work_holder *holder)
{
    work_holder_delete_with(holder, WORK_HOLDER_KEPT);
}

/* creates_0xe40_byte_object_with_0x020e9820 (game/engine/object_create_family.cpp): the owner's
   first field is an allocator whose slot 0x58 allocates. */
#define CREATE_OBJECT_SIZE 0xe40u
#define CREATE_OBJECT_ALIGNMENT 0x10u
#define CREATE_WRONG_ALIGNMENT 8u
#define CREATE_ALLOCATE_SLOT 11
#define CREATE_CONSTRUCT 0x020e9820u

typedef void *(*create_allocate)(void *, uint64_t, uint64_t);
typedef void (*create_construct)(void *, void *);

static void *create_with(void ***owner, uint64_t alignment, int construct_always)
{
    void **allocator = *owner;
    void *object = ((create_allocate)(*(void ***)allocator)[CREATE_ALLOCATE_SLOT])(allocator, CREATE_OBJECT_SIZE, alignment);
    if (object || construct_always)
        ((create_construct)rt_guest(CREATE_CONSTRUCT))(object, owner);
    return object;
}

/* Allocates with alignment 8 instead of 16. */
void *bb_mutant_create_object_aligned_8(void ***owner)
{
    return create_with(owner, CREATE_WRONG_ALIGNMENT, 0);
}

/* Constructs even when the allocation failed. */
void *bb_mutant_create_object_constructs_on_failure(void ***owner)
{
    return create_with(owner, CREATE_OBJECT_ALIGNMENT, 1);
}

/* Returns what the constructor returns instead of the object. */
void *bb_mutant_create_object_returns_constructor_result(void ***owner)
{
    void **allocator = *owner;
    void *object = ((create_allocate)(*(void ***)allocator)[CREATE_ALLOCATE_SLOT])(allocator, CREATE_OBJECT_SIZE,
                                                                                    CREATE_OBJECT_ALIGNMENT);
    if (!object)
        return 0;
    return ((void *(*)(void *, void *))rt_guest(CREATE_CONSTRUCT))(object, owner);
}

/* frees_block_at_0xa0_then_destroys_base_0x00569a90 (game/engine/block_at_0xa0_family.cpp): the
   block at +0xa0, its data, the free slot 0x68 of the allocator the global points at. */
#define BLOCK_GLOBAL_ALLOCATOR 0x057e7320u
#define BLOCK_FREE_SLOT 13
#define BLOCK_BASE_DESTROY 0x00569a90u

struct mutant_block_owner {
    uint8_t unknown_0x00[0xa0];
    void **block;
};
typedef void (*block_free)(void *, void *);

#define BLOCK_RIGHT 0
#define BLOCK_ONCE 1  /* frees the block read before the first free */
#define BLOCK_ALLOCATOR_ONCE 2  /* frees both through the allocator read before the first free */
#define BLOCK_SKIPS_DATA 4  /* frees only the block */

static void block_free_through(void **allocator, void *data)
{
    ((block_free)(*(void ***)allocator)[BLOCK_FREE_SLOT])(allocator, data);
}

static void block_destroy_with(struct mutant_block_owner *owner, int wrong)
{
    void **block = owner->block;
    if (block) {
        void **allocator = *(void **volatile *)rt_guest(BLOCK_GLOBAL_ALLOCATOR);
        if (!(wrong & BLOCK_SKIPS_DATA))
            block_free_through(allocator, block[0]);
        if (!(wrong & BLOCK_ALLOCATOR_ONCE))
            allocator = *(void **volatile *)rt_guest(BLOCK_GLOBAL_ALLOCATOR);
        block_free_through(allocator, (wrong & BLOCK_ONCE) ? block : *(void **volatile *)&owner->block);
    }
    ((void (*)(void *))rt_guest(BLOCK_BASE_DESTROY))(owner);
}

void bb_mutant_block_at_0xa0_once(struct mutant_block_owner *owner)
{
    block_destroy_with(owner, BLOCK_ONCE);
}

void bb_mutant_block_at_0xa0_allocator_once(struct mutant_block_owner *owner)
{
    block_destroy_with(owner, BLOCK_ALLOCATOR_ONCE);
}

void bb_mutant_block_at_0xa0_skips_data(struct mutant_block_owner *owner)
{
    block_destroy_with(owner, BLOCK_SKIPS_DATA);
}

/* destroys_owner_of_two_objects and deletes_owner_of_two_objects
   (game/engine/owner_of_two_family.cpp): the owner (vtable, first +0x8, second +0x10) and its
   objects, whose vtable slot 0x18 releases them. */
#define OWNER_VTABLE 0x0570f800u
#define OWNER_VTABLE_POINT 0x10u
#define OWNER_RELEASE_SLOT 3
#define OWNER_SIZED_FREE 0x02fa1c20u
#define OWNER_ALLOCATOR 0x05ac8288u
#define OWNER_SIZE 0x18u
#define OWNER_ALIGNMENT 8u

struct mutant_owned {
    void (**vtable)(struct mutant_owned *);
};
struct mutant_owner_of_two {
    const void *vtable;
    struct mutant_owned *first;
    struct mutant_owned *second;
};
typedef void (*owner_sized_free)(void *, void *, uint64_t, uint64_t);

#define OWNER_RIGHT 0
#define OWNER_SECOND_EARLY 1  /* reads the second object before the first release */
#define OWNER_FIRST_CLEARED_LATE 2  /* clears the first field only after both releases */
#define OWNER_NO_FINAL_CLEAR 4  /* does not clear the fields again at the end */
#define OWNER_SECOND_ALONE 8  /* releases the second object without a first one */
#define OWNER_ALLOCATOR_EARLY 16  /* reads the allocator before the releases */

static void owner_release(struct mutant_owned *object)
{
    object->vtable[OWNER_RELEASE_SLOT](object);
}

static void owner_destroy_with(struct mutant_owner_of_two *owner, int wrong)
{
    owner->vtable = (const uint8_t *)rt_guest(OWNER_VTABLE) + OWNER_VTABLE_POINT;
    struct mutant_owned *volatile *first = &owner->first;
    struct mutant_owned *volatile *second = &owner->second;
    if (!*first) {
        if ((wrong & OWNER_SECOND_ALONE) && *second)
            owner_release(*second);
        return;
    }
    struct mutant_owned *early = *second;
    owner_release(*first);
    if (!(wrong & OWNER_FIRST_CLEARED_LATE))
        *first = 0;
    struct mutant_owned *object = (wrong & OWNER_SECOND_EARLY) ? early : *second;
    if (object) {
        owner_release(object);
        *second = 0;
    }
    if (!(wrong & OWNER_NO_FINAL_CLEAR) || (wrong & OWNER_FIRST_CLEARED_LATE)) {
        *second = 0;
        *first = 0;
    }
}

static void owner_delete_with(struct mutant_owner_of_two *owner, int wrong)
{
    void *allocator = *(void *volatile *)rt_guest(OWNER_ALLOCATOR);
    owner_destroy_with(owner, wrong);
    if (!(wrong & OWNER_ALLOCATOR_EARLY))
        allocator = *(void *volatile *)rt_guest(OWNER_ALLOCATOR);
    ((owner_sized_free)rt_guest(OWNER_SIZED_FREE))(allocator, owner, OWNER_SIZE, OWNER_ALIGNMENT);
}

void bb_mutant_owner_of_two_second_early(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_SECOND_EARLY);
}

void bb_mutant_owner_of_two_first_cleared_late(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_FIRST_CLEARED_LATE);
}

void bb_mutant_owner_of_two_no_final_clear(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_NO_FINAL_CLEAR);
}

void bb_mutant_owner_of_two_second_alone(struct mutant_owner_of_two *owner)
{
    owner_destroy_with(owner, OWNER_SECOND_ALONE);
}

void bb_mutant_owner_of_two_delete_second_early(struct mutant_owner_of_two *owner)
{
    owner_delete_with(owner, OWNER_SECOND_EARLY);
}

void bb_mutant_owner_of_two_delete_first_cleared_late(struct mutant_owner_of_two *owner)
{
    owner_delete_with(owner, OWNER_FIRST_CLEARED_LATE);
}

void bb_mutant_owner_of_two_delete_allocator_early(struct mutant_owner_of_two *owner)
{
    owner_delete_with(owner, OWNER_ALLOCATOR_EARLY);
}

/* Mutants from #1177. */

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
