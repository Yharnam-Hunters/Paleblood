// SPDX-License-Identifier: GPL-2.0-or-later
// Five families of small destructors (symbols/families.csv), each the copies of one function:
//  - stores_vtable_releases_0x10_then_destroys: store the class's table address, release the
//    member at +0x10 (when set) and clear it, then tail-call the base destructor;
//  - stores_vtable_releases_0x10_destroys_then_deletes: its deleting twin;
//  - releases_member_at_0x20_through_0x01401720: release the member at +0x20 (when set) and clear it;
//  - releases_member_at_0x18_through_0x01401720: the same for the member at +0x18;
//  - destroys_then_frees_0x10_bytes_through_0x05ac8288: destroy the object, then hand its 0x10 bytes
//    (alignment 8) to the sized free of the allocator a global points at.
// The base destructor of the first two and the destructor of the last are empty functions (members
// of returns_nothing), so the replacements do not call them.
#include <cstddef>
#include <cstdint>

#include "callback_handle.h"
#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));
RT_ORIGINAL(0x01401720, release_member_through_0x01401720, void(void *member));
RT_ORIGINAL(0x02fa1c20, free_sized, uint64_t(void *allocator, void *object, uint64_t size, uint64_t alignment));
RT_GLOBAL(0x05ac8288, object_allocator, void *const);

constexpr uint32_t class_table = 0x057b1fb0;   // stored as is (no header offset)
constexpr uint64_t object_size = 0x10;
constexpr uint64_t object_alignment = 8;

struct Holder0x10 {
    const void *vtable;
    uint8_t unknown_0x8[0x8];
    void *member;
};
static_assert(offsetof(Holder0x10, member) == 0x10);

struct Holder0x20 {
    uint8_t unknown_0x0[0x20];
    void *member;
};
static_assert(offsetof(Holder0x20, member) == 0x20);

struct Holder0x18 {
    uint8_t unknown_0x0[0x18];
    void *member;
};
static_assert(offsetof(Holder0x18, member) == 0x18);

template <typename Holder>
void release_member_through_0x01401720_and_clear(void *object)
{
    auto *holder = static_cast<Holder *>(object);
    if (holder->member) {
        release_member_through_0x01401720(holder->member);
        holder->member = nullptr;
    }
}

void release_member_and_store_vtable(void *object)
{
    auto *holder = static_cast<Holder0x10 *>(object);
    holder->vtable = rt_guest(class_table);
    if (holder->member) {
        engine::callback_release_handle(holder->member);
        holder->member = nullptr;
    }
}

}  // namespace

extern "C" void bb_engine_stores_vtable_releases_0x10_then_destroys(void *object)
{
    release_member_and_store_vtable(object);
}

extern "C" void bb_engine_stores_vtable_releases_0x10_destroys_then_deletes(void *object)
{
    release_member_and_store_vtable(object);
    operator_delete(object);
}

extern "C" void bb_engine_releases_member_at_0x20_through_0x01401720(void *object)
{
    release_member_through_0x01401720_and_clear<Holder0x20>(object);
}

extern "C" void bb_engine_releases_member_at_0x18_through_0x01401720(void *object)
{
    release_member_through_0x01401720_and_clear<Holder0x18>(object);
}

extern "C" uint64_t bb_engine_destroys_then_frees_0x10_bytes_through_0x05ac8288(void *object)
{
    return free_sized(object_allocator.get(), object, object_size, object_alignment);
}
