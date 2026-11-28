// SPDX-License-Identifier: GPL-2.0-or-later
// Four families (symbols/families.csv), each the copies of one function:
//  - sets_property_0x3ea_or_0x3eb_from_variant: a property setter that takes a variant (a value
//    and its type); property 0x3ea stores the variant's number at +0x168, 0x3eb at +0x16c. A
//    variant that holds a reference-counted object is copied in and dropped again around it;
//  - copies_wide_string_at_0x28_on_thread_heap: copy-constructs the wide string at +0x28 of the
//    source on the calling thread's temporary heap, with its owns flag, and returns the copy;
//  - appends_blocks_to_pool_slot_5_or_16: appends a pair's two blocks of 4-byte elements to slot
//    5 (or 16) of a pool and records where each landed in the pool's map, keyed by the block;
//  - assigns_created_string_unless_exception: a scaleform VM thunk that makes a string from its
//    arguments and assigns it to the result unless the VM has raised an exception.
#include <immintrin.h>

#include <cstddef>
#include <cstdint>

#include "container.h"
#include "engine.h"
#include "ref_object.h"
#include "runtime/original.h"
#include "wide_string.h"

namespace {

// ---- the variant property setter -----------------------------------------------------------

// A variant: an 8-byte value (an object pointer, or a number in its low four bytes) and its type.
struct Variant {
    uint64_t value;
    int32_t type;
};
static_assert(offsetof(Variant, type) == 0x8);

constexpr int32_t variant_float = 1;
constexpr int32_t variant_integer = 2;
constexpr int32_t variant_object = 3;

constexpr int32_t property_at_0x168 = 0x3ea;
constexpr int32_t property_at_0x16c = 0x3eb;

struct PropertyOwner {
    uint8_t unknown_0x0[0x168];
    int32_t property_0x3ea;
    int32_t property_0x3eb;
};
static_assert(offsetof(PropertyOwner, property_0x3ea) == 0x168);
static_assert(offsetof(PropertyOwner, property_0x3eb) == 0x16c);

engine::RefObject *object_of(const Variant *variant)
{
    return reinterpret_cast<engine::RefObject *>(variant->value);
}

// The variant as a number: a float truncated toward zero (cvttss2si: out of range and NaN give
// INT32_MIN), an integer as it is, anything else 0.
int32_t number_of(int32_t type, uint32_t bits)
{
    if (type == variant_float)
        return _mm_cvttss_si32(_mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int32_t>(bits))));
    return type == variant_integer ? static_cast<int32_t>(bits) : 0;
}

// ---- the wide string copy -------------------------------------------------------------------

using WideString = engine::WideString;

// The source holds a wide string at +0x28 (its text, size, capacity and owns flag are read at
// the wide string's offsets from there).
struct WideStringHolder {
    uint8_t unknown_0x0[0x28];
    engine::WideString text;
};
static_assert(offsetof(WideStringHolder, text) == 0x28);

// The calling thread's temporary heap, created first when the thread has none: the allocator
// inside it.
RT_ORIGINAL(0x0247d7a0, thread_heap_allocator, engine::Allocator *());

const char16_t *characters_of(const engine::WideString &text)
{
    return text.capacity >= engine::wide_string_inline_capacity ? text.heap : text.inline_text;
}

// ---- the pool appends -----------------------------------------------------------------------

// A block of 4-byte elements to append: two of them, and their element count.
struct BlockPair {
    uint8_t unknown_0x0[0x8];
    const void *first;
    const void *second;
    uint32_t count;
};
static_assert(offsetof(BlockPair, first) == 0x8 && offsetof(BlockPair, second) == 0x10);
static_assert(offsetof(BlockPair, count) == 0x18);

constexpr uint32_t element_bytes = 4;
constexpr uint32_t selected_slot = 16;
constexpr uint32_t default_slot = 5;

// The pool: its map from block to copy first, then per slot a base pointer (from +0x8) and a
// 32-bit used size (from +0xb8).
struct PoolMap;
struct Pool {
    PoolMap *map;
};
constexpr std::ptrdiff_t pool_bases_offset = 0x8;
constexpr std::ptrdiff_t pool_used_offset = 0xb8;

uint8_t *&base_of(Pool *pool, uint32_t slot)
{
    uint8_t *const bytes = reinterpret_cast<uint8_t *>(pool) + pool_bases_offset;
    return reinterpret_cast<uint8_t **>(bytes)[slot];
}

uint32_t &used_of(Pool *pool, uint32_t slot)
{
    uint8_t *const bytes = reinterpret_cast<uint8_t *>(pool) + pool_used_offset;
    return reinterpret_cast<uint32_t *>(bytes)[slot];
}

RT_ORIGINAL(0x02a1a1b0, engine_memcpy, void(void *destination, const void *source, int64_t count));
// The map's entry for a key (made when missing): where its value goes.
RT_ORIGINAL(0x02edc8e0, map_entry, uint8_t **(PoolMap *map, const void *const *key));

// Copies the block to the end of the slot's data, moves the used size on (read again after the
// copy) and records the copy in the map under the block's address.
void append_block(Pool *pool, uint32_t slot, const void *block, uint32_t count)
{
    const uint32_t bytes = count * element_bytes;
    uint8_t *const destination = base_of(pool, slot) + used_of(pool, slot);
    engine_memcpy(destination, block, bytes);
    used_of(pool, slot) += bytes;
    const void *key = block;
    *map_entry(pool->map, &key) = destination;
}

// ---- the scaleform string thunk -------------------------------------------------------------

// A string node: its reference count at +0x18 (not atomic).
struct StringNode {
    uint8_t unknown_0x0[0x18];
    int32_t references;
};
static_assert(offsetof(StringNode, references) == 0x18);

// The string manager holds the empty string's node at +0x48.
struct StringManager {
    uint8_t unknown_0x0[0x48];
    StringNode empty;
};
static_assert(offsetof(StringManager, empty) == 0x48);

struct VmOwner {
    uint8_t unknown_0x0[0x218];
    StringManager *strings;
};
static_assert(offsetof(VmOwner, strings) == 0x218);

struct Vm {
    uint8_t unknown_0x0[0x10];
    VmOwner *owner;
    uint8_t unknown_0x18[0x88];
    uint8_t exception;
};
static_assert(offsetof(Vm, owner) == 0x10 && offsetof(Vm, exception) == 0xa0);

// The argument object, and the chain of objects that leads to the second input of the string
// maker (+0x28, then +0x80, then +0x10; the input is 0x70 bytes into the last one).
struct MakerContext;
struct MakerInfo {
    uint8_t unknown_0x0[0x10];
    MakerContext *context;
};
static_assert(offsetof(MakerInfo, context) == 0x10);
struct MakerLink {
    uint8_t unknown_0x0[0x80];
    MakerInfo *info;
};
static_assert(offsetof(MakerLink, info) == 0x80);
struct ArgumentObject {
    uint8_t unknown_0x0[0x28];
    MakerLink *link;
};
static_assert(offsetof(ArgumentObject, link) == 0x28);
struct Arguments {
    uint8_t unknown_0x0[0x10];
    ArgumentObject *object;
};
static_assert(offsetof(Arguments, object) == 0x10);
constexpr std::ptrdiff_t object_input_offset = 0x40;
constexpr std::ptrdiff_t context_input_offset = 0x70;

struct Value;

RT_ORIGINAL(0x00671390, make_string, void(StringNode **out, void *object_input, void *context_input));
// Frees a node whose last reference went.
RT_ORIGINAL(0x004f0410, release_node, void(StringNode *node));
// Stores the string in the value (taking its own reference).
RT_ORIGINAL(0x007183d0, assign_string, void(Value *value, StringNode *const *node));

void drop(StringNode *node)
{
    if (--node->references == 0)
        release_node(node);
}

void *input_at(void *base, std::ptrdiff_t offset)
{
    return static_cast<uint8_t *>(base) + offset;
}

}  // namespace

// The variant is read again after the reference is taken; the reference is dropped with the
// object read then (the destroy or the invalid-unref report comes last).
extern "C" void bb_engine_sets_property_0x3ea_or_0x3eb_from_variant(PropertyOwner *owner, int32_t property,
                                                                    const Variant *variant)
{
    const int32_t type = variant->type;
    if (type == variant_object)
        engine::retain(object_of(variant));
    engine::RefObject *const object = object_of(variant);
    const uint32_t bits = static_cast<uint32_t>(variant->value);
    if (property == property_at_0x16c)
        owner->property_0x3eb = number_of(type, bits);
    else if (property == property_at_0x168)
        owner->property_0x3ea = number_of(type, bits);
    if (type == variant_object)
        engine::release(object);
}

// The text and size are taken before the heap is fetched and checked; the owns flag after the
// assignment.
extern "C" WideString *bb_engine_copies_wide_string_at_0x28_on_thread_heap(WideString *copy,
                                                                          const WideStringHolder *source)
{
    const char16_t *const characters = characters_of(source->text);
    const uint64_t size = source->text.size;
    engine::Allocator *const heap = thread_heap_allocator();
    engine::check_container_heap(heap);
    copy->allocator = heap;
    copy->capacity = engine::wide_string_inline_capacity - 1;
    copy->size = 0;
    copy->inline_text[0] = 0;
    engine::wide_string_assign(copy, characters, size);
    copy->owns = source->text.owns;
    return copy;
}

// The count is read again after the first block (whose map entry may change it), the second
// block's address only then. The first argument is not used.
extern "C" bool bb_engine_appends_blocks_to_pool_slot_5_or_16(void *, Pool *pool, const BlockPair *pair,
                                                               uint64_t select_slot_16)
{
    const uint32_t slot = static_cast<uint8_t>(select_slot_16) != 0 ? selected_slot : default_slot;
    const void *const first = pair->first;
    uint32_t count = pair->count;
    if (first) {
        append_block(pool, slot, first, count);
        count = pair->count;
    }
    const void *const second = pair->second;
    if (second)
        append_block(pool, slot, second, count);
    return true;
}

// Holds the empty string, takes the made string over it (as an assignment: the new reference
// first, then the old dropped), drops the maker's reference, assigns unless the VM raised an
// exception (read after those drops), then drops the held string (read again after the assign).
// The first argument is not used.
extern "C" void bb_engine_assigns_created_string_unless_exception(void *, Vm *vm, const Arguments *arguments,
                                                                  Value *result)
{
    ArgumentObject *const object = arguments->object;
    StringNode *const empty = &vm->owner->strings->empty;
    StringNode *held = empty;
    ++held->references;
    MakerContext *const context = object->link->info->context;
    StringNode *made = nullptr;
    make_string(&made, input_at(object, object_input_offset), input_at(context, context_input_offset));
    StringNode *const created = made;
    ++created->references;
    drop(empty);
    held = created;
    drop(created);
    if (!vm->exception)
        assign_string(result, &held);
    drop(held);
}
