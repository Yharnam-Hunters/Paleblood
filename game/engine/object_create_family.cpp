// SPDX-License-Identifier: GPL-2.0-or-later
// creates_0xe40_byte_object_with_0x020e9820 (symbols/families.csv): the copies of one factory
// (thirteen functions side by side, 0x40 bytes apart). Each asks the allocator the owner's first
// field points at for 0xe40 bytes aligned to 16 and, when it gets them, constructs the object there
// with the owner (0x020e9820); it returns the object, or null when the allocation fails. What the
// object is, and what the constructor does, is not traced here.
#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "runtime/original.h"

namespace {

struct Owner {
    engine::Allocator *allocator;
};
static_assert(offsetof(Owner, allocator) == 0x0);

RT_ORIGINAL(0x020e9820, construct_object, void(void *object, Owner *owner));

constexpr uint64_t object_size = 0xe40;
constexpr uint64_t object_alignment = 0x10;

}  // namespace

extern "C" void *bb_engine_creates_0xe40_byte_object_with_0x020e9820(Owner *owner)
{
    void *object = owner->allocator->allocate(object_size, object_alignment);
    if (!object)
        return nullptr;
    construct_object(object, owner);
    return object;
}
