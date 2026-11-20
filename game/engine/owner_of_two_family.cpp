// SPDX-License-Identifier: GPL-2.0-or-later
// Two families (symbols/families.csv) of destructors of one engine class (vtable 0x0570f800),
// which owns two objects at +0x8 and +0x10:
// - destroys_owner_of_two_objects: the class's vtable, then, only when there is a first object,
//   the first object's release (its vtable slot 0x18) and the first field cleared; the second
//   field, read after that, likewise when it holds an object; finally both fields are cleared
//   again (a release can have stored into them);
// - deletes_owner_of_two_objects: the same, then the 0x18-byte object goes to the sized free
//   (0x02fa1c20, alignment 8) of the allocator the global 0x05ac8288 points at, read after the
//   releases.
// A missing first object leaves the second one alone. Which class this is is not traced here.
#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace {

struct OwnedObject;
struct OwnedObjectVtable {
    void *unknown_slots[3];
    void (*release)(OwnedObject *);
};
static_assert(offsetof(OwnedObjectVtable, release) == 0x18);
struct OwnedObject {
    const OwnedObjectVtable *vtable;
};

struct OwnerOfTwo {
    const uint8_t *vtable;
    OwnedObject *first;
    OwnedObject *second;
};
static_assert(offsetof(OwnerOfTwo, first) == 0x8 && offsetof(OwnerOfTwo, second) == 0x10);
static_assert(sizeof(OwnerOfTwo) == 0x18);

RT_GLOBAL(0x0570f800, owner_of_two_vtable, const uint8_t);
RT_ORIGINAL(0x02fa1c20, sized_free, void(void *allocator, void *data, uint64_t size, uint64_t alignment));
RT_GLOBAL(0x05ac8288, owner_allocator, void *const);
constexpr uint64_t owner_alignment = 8;

void release(OwnedObject *object)
{
    object->vtable->release(object);
}

void destroy(OwnerOfTwo *owner)
{
    owner->vtable = owner_of_two_vtable.address() + engine::vtable_address_point;
    if (!owner->first)
        return;
    release(owner->first);
    owner->first = nullptr;
    if (OwnedObject *second = owner->second) {
        release(second);
        owner->second = nullptr;
    }
    owner->second = nullptr;
    owner->first = nullptr;
}

}  // namespace

extern "C" void bb_engine_destroys_owner_of_two_objects(OwnerOfTwo *owner)
{
    destroy(owner);
}

extern "C" void bb_engine_deletes_owner_of_two_objects(OwnerOfTwo *owner)
{
    destroy(owner);
    sized_free(owner_allocator.get(), owner, sizeof(OwnerOfTwo), owner_alignment);
}
