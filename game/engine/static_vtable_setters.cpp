// SPDX-License-Identifier: GPL-2.0-or-later
// Three families of guarded static initializers (sets_static_vtable_at_* in
// symbols/families.csv), each the copies of one function: when the guard's low byte is clear,
// the static object gets its class's vtable (the address points past the table's two header
// entries) and the guard becomes 1, stored as a qword.
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
