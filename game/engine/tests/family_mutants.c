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
