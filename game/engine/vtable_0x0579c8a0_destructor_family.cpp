// SPDX-License-Identifier: GPL-2.0-or-later
// The families stores_vtable_0x0579c8a0_releases_members_then_destroys and its deleting twin
// (symbols/families.csv, docs/FAMILIES.md): copies of the destructor of one class whose tables
// start at 0x0579c8a0 (the object's own table +0x10, a second one at +0x48 from +0x90). The copies
// tear the object down in this order:
// - the member at +0x1f20 (table 0x0579a0f0): its callable is destroyed through slot +0x20 (told
//   whether it lives outside its inline storage) and cleared; its wide string is released and
//   emptied; its held block at +0x1f30 is released (below);
// - the member at +0xd90, through 0x01f4cbb0;
// - the base (tables 0x0579a880 +0x10 and +0x78): its held block at +0xd30 is released, the member
//   at +0xa70 goes through 0x01ed24f0, and the rest through the base destructor 0x01ed5390.
// A held block (table 0x0579a060) with bit 0x40 set hands its area and value to its releaser's
// slot +0x18, then forgets the releaser; its flags end at zero either way. The deleting twin then
// deletes the object (0x02fbe588). Every field is read where the code reads it, after the calls
// before it.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"
#include "wide_string.h"

namespace {

// A callable kept in a slot with inline storage: destroyed through its table's slot +0x20, which is
// told whether the callable sits outside the slot's storage (and so must free itself).
struct Callable;
struct CallableVtable {
    void *unknown_slots[4];
    void (*destroy)(Callable *, bool outside_storage);
};
static_assert(offsetof(CallableVtable, destroy) == 0x20);
struct Callable {
    const CallableVtable *vtable;
};
constexpr std::size_t callable_storage_size = 0x20;  // the slot's inline storage, before the pointer
struct CallableSlot {
    uint8_t storage[callable_storage_size];
    Callable *callable;
};
static_assert(offsetof(CallableSlot, callable) == 0x20);

// A held block: when bit 0x40 of its flags is set, its releaser's slot +0x18 takes the block's
// area (by address) and value.
struct Releaser;
struct ReleaserVtable {
    void *unknown_slots[3];
    void (*release)(Releaser *, void *area, uint64_t value);
};
static_assert(offsetof(ReleaserVtable, release) == 0x18);
struct Releaser {
    const ReleaserVtable *vtable;
};
constexpr std::size_t held_area_size = 0x10;  // from the area (handed over by address) to the releaser
struct HeldBlock {
    const void *vtable;
    uint8_t unknown_0x08[0x28];
    uint8_t area[held_area_size];
    Releaser *releaser;
    uint32_t flags;
    uint8_t unknown_0x4c[0x4];
    uint64_t value;
};
static_assert(offsetof(HeldBlock, area) == 0x30 && offsetof(HeldBlock, releaser) == 0x40);
static_assert(offsetof(HeldBlock, flags) == 0x48 && offsetof(HeldBlock, value) == 0x50);
constexpr uint32_t held_block_set = 0x40;

// The member at +0x1f20: its own table, a held block, a wide string and a callable slot.
struct Member {
    const void *vtable;
    uint8_t unknown_0x08[0x8];
    HeldBlock held;
    uint8_t unknown_0x68[0x28];
    engine::WideString text;
    uint8_t unknown_0xc8[0x8];
    CallableSlot callable;
};
static_assert(offsetof(Member, held) == 0x10 && offsetof(Member, text) == 0x90);
static_assert(offsetof(Member, callable) == 0xd0);

// The object: two table pointers (+0x0 and +0x48), the base's member at +0xa70 and held block at
// +0xd30, the members at +0xd90 and +0x1f20; the rest is unknown.
struct Object {
    const void *vtable;
    uint8_t unknown_0x08[0x40];
    const void *second_vtable;
    uint8_t unknown_0x50[0xa20];
    uint8_t unknown_0xa70[0x2c0];   // the base's member destroyed out of line
    HeldBlock base_held;
    uint8_t unknown_0xd88[0x8];
    uint8_t unknown_0xd90[0x1190];  // the member destroyed out of line
    Member second_member;
};
static_assert(offsetof(Object, second_vtable) == 0x48 && offsetof(Object, unknown_0xa70) == 0xa70);
static_assert(offsetof(Object, base_held) == 0xd30 && offsetof(Object, unknown_0xd90) == 0xd90);
static_assert(offsetof(Object, second_member) == 0x1f20);

// The tables (each pointer stored is a group's start plus an offset).
RT_GLOBAL(0x0579c8a0, object_tables, const uint8_t);
RT_GLOBAL(0x0579a880, base_tables, const uint8_t);
RT_GLOBAL(0x0579a0f0, member_tables, const uint8_t);
RT_GLOBAL(0x0579a060, held_block_tables, const uint8_t);
constexpr std::ptrdiff_t table_start = 0x10;
constexpr std::ptrdiff_t object_second_table = 0x90;
constexpr std::ptrdiff_t base_second_table = 0x78;

RT_ORIGINAL(0x01f4cbb0, destroy_first_member, void(void *member));
RT_ORIGINAL(0x01ed24f0, destroy_base_member, void(void *member));
RT_ORIGINAL(0x01ed5390, destroy_base, void(Object *));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

void release_held_block(HeldBlock *held)
{
    held->vtable = held_block_tables.address() + table_start;
    if (held->flags & held_block_set) {
        held->releaser->vtable->release(held->releaser, held->area, held->value);
        held->releaser = nullptr;
    }
    held->flags = 0;
}

void destroy_member(Member *member)
{
    member->vtable = member_tables.address() + table_start;
    if (Callable *callable = member->callable.callable) {
        callable->vtable->destroy(callable, callable != reinterpret_cast<Callable *>(member->callable.storage));
        member->callable.callable = nullptr;
    }
    engine::wide_string_release(&member->text);
    member->text.capacity = engine::wide_string_inline_capacity - 1;
    member->text.size = 0;
    member->text.inline_text[0] = u'\0';
    release_held_block(&member->held);
}

void destroy(Object *object)
{
    object->vtable = object_tables.address() + table_start;
    object->second_vtable = object_tables.address() + object_second_table;
    destroy_member(&object->second_member);
    destroy_first_member(object->unknown_0xd90);
    object->vtable = base_tables.address() + table_start;
    object->second_vtable = base_tables.address() + base_second_table;
    release_held_block(&object->base_held);
    destroy_base_member(object->unknown_0xa70);
    destroy_base(object);
}

}  // namespace

extern "C" void bb_engine_stores_vtable_0x0579c8a0_releases_members_then_destroys(Object *object)
{
    destroy(object);
}

extern "C" void bb_engine_stores_vtable_0x0579c8a0_releases_members_destroys_then_deletes(Object *object)
{
    destroy(object);
    operator_delete(object);
}
