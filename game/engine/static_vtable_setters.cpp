// SPDX-License-Identifier: GPL-2.0-or-later
// Families that store a class's vtable (symbols/families.csv), each the copies of one function:
//  - sets_static_vtable_at_*: guarded static initializers: when the guard's low byte is clear,
//    the static object gets its class's vtable (the address points past the table's two header
//    entries) and the guard becomes 1, stored as a qword.
//  - stores_vtable_0x05541b90_then_releases_member_at_8: the object rdi points at gets the
//    vtable past its header, then the object its pointer at +8 points at, if any, has its slot
//    0x18 called.
#include <cstddef>
#include <cstdint>

#include "runtime/guest.h"

namespace {

constexpr std::size_t vtable_header = 0x10;   // offset to top and type info before the entries

// A static object (its address), its guard and its class's vtable.
struct StaticVtable {
    uint32_t object;
    uint32_t guard;
    uint32_t vtable;
};

void set_static_vtable(const StaticVtable &setter)
{
    auto *guard = static_cast<uint64_t *>(rt_guest(setter.guard));
    if (static_cast<uint8_t>(*guard) != 0)
        return;
    *static_cast<const void **>(rt_guest(setter.object)) =
        static_cast<const uint8_t *>(rt_guest(setter.vtable)) + vtable_header;
    *guard = 1;
}

constexpr StaticVtable static_object_05ac59b0{0x05ac59b0, 0x05ac59b8, 0x056b4840};
constexpr StaticVtable static_object_05ac59c0{0x05ac59c0, 0x05ac59c8, 0x056b4890};
constexpr StaticVtable static_object_05ac59d0{0x05ac59d0, 0x05ac59d8, 0x056b47f0};

// An object that holds another at +8, released through slot 0x18 of that object's table.
using ReleaseFunction = void (*)(void *member);

struct Member {
    const ReleaseFunction *table;
};

struct OwningObject {
    const void *vtable;
    Member *member;
};

constexpr uint32_t owning_object_vtable = 0x05541b90;
constexpr std::size_t release_slot = 0x18;

}  // namespace

extern "C" void bb_engine_sets_static_vtable_at_0x05ac59b0()
{
    set_static_vtable(static_object_05ac59b0);
}

extern "C" void bb_engine_sets_static_vtable_at_0x05ac59c0()
{
    set_static_vtable(static_object_05ac59c0);
}

extern "C" void bb_engine_sets_static_vtable_at_0x05ac59d0()
{
    set_static_vtable(static_object_05ac59d0);
}

extern "C" void bb_engine_stores_vtable_0x05541b90_then_releases_member_at_8(OwningObject *object)
{
    object->vtable = static_cast<const uint8_t *>(rt_guest(owning_object_vtable)) + vtable_header;
    Member *member = object->member;
    if (member != nullptr)
        member->table[release_slot / sizeof(ReleaseFunction)](member);
}
