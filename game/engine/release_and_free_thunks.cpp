// SPDX-License-Identifier: GPL-2.0-or-later
// Eight engine families (symbols/families.csv), each the copies of one function that reach the
// same functions and globals (a relative shape):
//  - destroys_second_argument_then_frees_it_to_owning_heap: when the second argument is set,
//    destroy it (its table's first slot) and free it through the heap that owns it;
//  - stores_vtable_0x05541c80_releases_members_0x10_and_0x8 and its deleting twin
//    stores_vtable_0x05541c80_releases_members_then_deletes, and
//    stores_vtable_0x05541b90_releases_member_at_8_then_deletes: the destructors of a class that
//    holds a counted object at +0x8 and of the class derived from it, which holds one at +0x10;
//    each store of a table is followed by a release (slot 0x18) of that class's member;
//  - frees_string_at_0x10_through_allocator_at_0x30_then_deletes: free a string's heap buffer
//    through its allocator, then delete the object;
//  - stores_minus_2_at_0x10_through_setter_then_returns_zero: the setter it calls is itself a
//    member of writes_dword_field, so the store is made here;
//  - calls_0x022dc040_then_sets_byte_0x60: a call, then a flag;
//  - frees_0x10_bytes_through_0x05ac8288: free a 0x10-byte block through the allocator a global
//    points at.
#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

// Past the offset to top and the type information: where an object's table pointer points.
constexpr uint32_t table_past_header = 0x10;

RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

// ---- an object destroyed by its first slot, freed through the heap that owns it --------------

struct Destroyable;
struct DestroyableVtable {
    void (*destroy)(Destroyable *object);
};
struct Destroyable {
    const DestroyableVtable *vtable;
};

RT_ORIGINAL(0x0247b720, heap_owning, engine::Allocator *(const void *object));

// ---- the class with a counted member at +0x8, and the class derived from it ------------------

struct Counted;
struct CountedVtable {
    void *unknown_slots_0x0[3];
    void (*release)(Counted *object);
};
static_assert(offsetof(CountedVtable, release) == 0x18);
struct Counted {
    const CountedVtable *vtable;
};

struct CountedHolder {
    const void *vtable;
    Counted *base_member;
    Counted *derived_member;
};
static_assert(offsetof(CountedHolder, base_member) == 0x8);
static_assert(offsetof(CountedHolder, derived_member) == 0x10);

constexpr uint32_t counted_holder_class = 0x05541b90;
constexpr uint32_t derived_counted_holder_class = 0x05541c80;

void release(Counted *member)
{
    if (member)
        member->vtable->release(member);
}

void destroy_counted_holder(CountedHolder *object)
{
    object->vtable = rt_guest(counted_holder_class + table_past_header);
    release(object->base_member);
}

void destroy_derived_counted_holder(CountedHolder *object)
{
    object->vtable = rt_guest(derived_counted_holder_class + table_past_header);
    release(object->derived_member);
    destroy_counted_holder(object);
}

// ---- a string whose buffer is on the heap from 8 units of capacity ---------------------------

struct StringOwner {
    uint8_t unknown_0x0[0x10];
    void *buffer;
    uint8_t unknown_0x18[0x10];
    uint64_t capacity;
    engine::Allocator *allocator;
};
static_assert(offsetof(StringOwner, buffer) == 0x10);
static_assert(offsetof(StringOwner, capacity) == 0x28);
static_assert(offsetof(StringOwner, allocator) == 0x30);
constexpr uint64_t heap_buffer_capacity = 8;

// ---- a stored constant, and a call then a flag --------------------------------------------------------------------

struct SettableObject {
    uint8_t unknown_0x0[0x10];
    int32_t value;
};
static_assert(offsetof(SettableObject, value) == 0x10);
constexpr int32_t minus_two = -2;

struct FlaggedObject {
    uint8_t unknown_0x0[0x60];
    uint8_t flag;
};
static_assert(offsetof(FlaggedObject, flag) == 0x60);
constexpr uint8_t flag_set = 1;
RT_ORIGINAL(0x022dc040, call_0x022dc040, void(FlaggedObject *object));

// ---- a block freed through the allocator a global points at ----------------------------------

constexpr uint64_t block_bytes = 0x10;
constexpr uint64_t block_alignment = 8;
RT_ORIGINAL(0x02fa1c20, free_sized, uint64_t(void *allocator, void *block, uint64_t size, uint64_t alignment));
RT_GLOBAL(0x05ac8288, block_allocator, void *const);

}  // namespace

extern "C" void bb_engine_destroys_second_argument_then_frees_it_to_owning_heap(void *, Destroyable *object)
{
    if (!object)
        return;
    engine::Allocator *heap = heap_owning(object);
    object->vtable->destroy(object);
    heap->free(object);
}

extern "C" void bb_engine_stores_vtable_0x05541c80_releases_members_0x10_and_0x8(CountedHolder *object)
{
    destroy_derived_counted_holder(object);
}

extern "C" void bb_engine_stores_vtable_0x05541c80_releases_members_then_deletes(CountedHolder *object)
{
    destroy_derived_counted_holder(object);
    operator_delete(object);
}

extern "C" void bb_engine_stores_vtable_0x05541b90_releases_member_at_8_then_deletes(CountedHolder *object)
{
    destroy_counted_holder(object);
    operator_delete(object);
}

extern "C" void bb_engine_frees_string_at_0x10_through_allocator_at_0x30_then_deletes(StringOwner *object)
{
    if (object->capacity >= heap_buffer_capacity)
        object->allocator->free(object->buffer);
    operator_delete(object);
}

extern "C" uint32_t bb_engine_stores_minus_2_at_0x10_through_setter_then_returns_zero(SettableObject *object)
{
    object->value = minus_two;
    return 0;
}

extern "C" void bb_engine_calls_0x022dc040_then_sets_byte_0x60(FlaggedObject *object)
{
    call_0x022dc040(object);
    object->flag = flag_set;
}

extern "C" uint64_t bb_engine_frees_0x10_bytes_through_0x05ac8288(void *block)
{
    return free_sized(block_allocator.get(), block, block_bytes, block_alignment);
}
