// SPDX-License-Identifier: GPL-2.0-or-later
// Twelve families of destructors (symbols/families.csv), each the copies of one function. All destroy
// an embedded member with one destructor and then the object's base with another:
//  - destroys_member_0x68_then_base: the member at +0x68, then the base;
//  - destroys_member_0x70_releases_0x68_then_base: the member at +0x70, then store the class's two
//    tables (at +0 and at the secondary base at +0x20), release the member at +0x68 when set, then
//    the base;
//  - ..._and_base_then_frees: the deleting twins, which then hand the object to the free (slot 0x68)
//    of the allocator a global points at and return its result;
//  - adjusts_<N>_...: the thunks reached through the base at +N, which first step back to the object.
#include <cstddef>
#include <cstdint>

#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x005c9010, destroy_member, void(void *member));
RT_ORIGINAL(0x005696e0, destroy_base, void(void *object));
RT_ORIGINAL(0x0045a4c0, release_member, void(void *member));

struct Allocator;
using Free = uint64_t (*)(Allocator *allocator, void *object);
struct Allocator {
    const Free *vtable;
};
constexpr std::size_t free_slot = 0x68;
RT_GLOBAL(0x057e7320, object_allocator, Allocator *const);

constexpr uint32_t class_tables = 0x055584e0;
constexpr uint32_t primary_table = 0x10;     // offsets into the class's tables
constexpr uint32_t secondary_table = 0xe8;

// The object of destroys_member_0x68_then_base and its kin.
struct MemberAt0x68 {
    uint8_t unknown_0x0[0x68];
    uint8_t member;   // an object destroyed in place
};

// The object of destroys_member_0x70_releases_0x68_then_base and its kin.
struct MemberAt0x70 {
    const void *vtable;
    uint8_t unknown_0x8[0x18];
    const void *secondary_vtable;
    uint8_t unknown_0x28[0x40];
    void *released;
    uint8_t member;   // an object destroyed in place
};
static_assert(offsetof(MemberAt0x68, member) == 0x68);
static_assert(offsetof(MemberAt0x70, secondary_vtable) == 0x20);
static_assert(offsetof(MemberAt0x70, released) == 0x68);
static_assert(offsetof(MemberAt0x70, member) == 0x70);

constexpr std::size_t no_adjustment = 0;
constexpr std::size_t secondary_base = offsetof(MemberAt0x70, secondary_vtable);
constexpr std::size_t member_base_0x68 = offsetof(MemberAt0x68, member);
constexpr std::size_t member_base_0x70 = offsetof(MemberAt0x70, member);

// A thunk's argument points at the base at +ADJUST; the object starts ADJUST bytes before it.
template <std::size_t Adjust>
void *object_of(void *base)
{
    return static_cast<uint8_t *>(base) - Adjust;
}

uint64_t free_object(void *object)
{
    Allocator *allocator = object_allocator.get();
    return allocator->vtable[free_slot / sizeof(Free)](allocator, object);
}

void destroy_member_0x68(void *object)
{
    auto *self = static_cast<MemberAt0x68 *>(object);
    destroy_member(&self->member);
    destroy_base(self);
}

void destroy_member_0x70(void *object)
{
    auto *self = static_cast<MemberAt0x70 *>(object);
    destroy_member(&self->member);
    self->vtable = rt_guest(class_tables + primary_table);
    self->secondary_vtable = rt_guest(class_tables + secondary_table);
    if (self->released)
        release_member(self->released);
    destroy_base(self);
}

template <std::size_t Adjust>
void destroy_member_0x68_from(void *base)
{
    destroy_member_0x68(object_of<Adjust>(base));
}

template <std::size_t Adjust>
uint64_t destroy_member_0x68_then_free_from(void *base)
{
    void *object = object_of<Adjust>(base);
    destroy_member_0x68(object);
    return free_object(object);
}

template <std::size_t Adjust>
void destroy_member_0x70_from(void *base)
{
    destroy_member_0x70(object_of<Adjust>(base));
}

template <std::size_t Adjust>
uint64_t destroy_member_0x70_then_free_from(void *base)
{
    void *object = object_of<Adjust>(base);
    destroy_member_0x70(object);
    return free_object(object);
}

}  // namespace

extern "C" void bb_engine_destroys_member_0x68_then_base(void *object)
{
    destroy_member_0x68_from<no_adjustment>(object);
}

extern "C" uint64_t bb_engine_destroys_member_0x68_and_base_then_frees(void *object)
{
    return destroy_member_0x68_then_free_from<no_adjustment>(object);
}

extern "C" void bb_engine_adjusts_0x20_destroys_member_0x68_then_base(void *base)
{
    destroy_member_0x68_from<secondary_base>(base);
}

extern "C" uint64_t bb_engine_adjusts_0x20_destroys_member_0x68_and_base_then_frees(void *base)
{
    return destroy_member_0x68_then_free_from<secondary_base>(base);
}

extern "C" void bb_engine_adjusts_0x68_destroys_member_0x68_then_base(void *base)
{
    destroy_member_0x68_from<member_base_0x68>(base);
}

extern "C" uint64_t bb_engine_adjusts_0x68_destroys_member_0x68_and_base_then_frees(void *base)
{
    return destroy_member_0x68_then_free_from<member_base_0x68>(base);
}

extern "C" void bb_engine_destroys_member_0x70_releases_0x68_then_base(void *object)
{
    destroy_member_0x70_from<no_adjustment>(object);
}

extern "C" uint64_t bb_engine_destroys_member_0x70_releases_0x68_and_base_then_frees(void *object)
{
    return destroy_member_0x70_then_free_from<no_adjustment>(object);
}

extern "C" void bb_engine_adjusts_0x20_destroys_member_0x70_releases_0x68_then_base(void *base)
{
    destroy_member_0x70_from<secondary_base>(base);
}

extern "C" uint64_t bb_engine_adjusts_0x20_destroys_member_0x70_releases_0x68_and_base_then_frees(void *base)
{
    return destroy_member_0x70_then_free_from<secondary_base>(base);
}

extern "C" void bb_engine_adjusts_0x70_destroys_member_0x70_releases_0x68_then_base(void *base)
{
    destroy_member_0x70_from<member_base_0x70>(base);
}

extern "C" uint64_t bb_engine_adjusts_0x70_destroys_member_0x70_releases_0x68_and_base_then_frees(void *base)
{
    return destroy_member_0x70_then_free_from<member_base_0x70>(base);
}
