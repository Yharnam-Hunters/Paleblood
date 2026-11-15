// SPDX-License-Identifier: GPL-2.0-or-later
// Two families of deleting destructors (symbols/families.csv), each the copies of one function:
//  - destroys_then_deletes: the object's destructor is one shared function, called before the
//    object goes to operator delete;
//  - calls_virtual_0x28_then_deletes_if_asked: the object's destructor is slot 0x28 of its
//    table, and the object goes to operator delete only when the second argument's low byte is
//    set.
//  - destroys_with_<D>_then_deletes: like destroys_then_deletes, with the destructor D;
//  - destroys_with_<D>_then_frees: the destructor D, then the object goes to the free (slot
//    0x68) of the allocator the global 0x057e7320 points at.
// All end in a tail call.
#include <cstddef>
#include <cstdint>

#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x024b5a60, shared_destructor, void(void *object));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

struct Allocator;
using Free = uint64_t (*)(Allocator *allocator, void *object);
struct Allocator {
    const Free *vtable;
};
constexpr std::size_t free_slot = 0x68;
RT_GLOBAL(0x057e7320, object_allocator, Allocator *const);

using VirtualDestructor = void (*)(void *object);
constexpr std::size_t destructor_slot = 0x28;

RT_ORIGINAL(0x01402180, destructor_01402180, void(void *object));
RT_ORIGINAL(0x010e7ca0, destructor_010e7ca0, void(void *object));
RT_ORIGINAL(0x01401dc0, destructor_01401dc0, void(void *object));
RT_ORIGINAL(0x01640e70, destructor_01640e70, void(void *object));
RT_ORIGINAL(0x0139c4f0, destructor_0139c4f0, void(void *object));
RT_ORIGINAL(0x007164e0, destructor_007164e0, void(void *object));
RT_ORIGINAL(0x005696e0, destructor_005696e0, void(void *object));
RT_ORIGINAL(0x006fa840, destructor_006fa840, void(void *object));
RT_ORIGINAL(0x0045a480, destructor_0045a480, void(void *object));

template <typename Destroy>
void destroy_then_delete(const Destroy &destroy, void *object)
{
    destroy(object);
    operator_delete(object);
}

template <typename Destroy>
uint64_t destroy_then_free(const Destroy &destroy, void *object)
{
    destroy(object);
    Allocator *allocator = object_allocator.get();
    return allocator->vtable[free_slot / sizeof(Free)](allocator, object);
}

}  // namespace

extern "C" void bb_engine_destroys_then_deletes(void *object)
{
    shared_destructor(object);
    operator_delete(object);
}

// The flag is the original's sil: only the low byte counts.
extern "C" void bb_engine_calls_virtual_0x28_then_deletes_if_asked(void *object, uint8_t delete_object)
{
    const uint8_t *table = *static_cast<uint8_t *const *>(object);
    (*reinterpret_cast<const VirtualDestructor *>(table + destructor_slot))(object);
    if (delete_object != 0)
        operator_delete(object);
}

extern "C" void bb_engine_destroys_with_0x01402180_then_deletes(void *object)
{
    destroy_then_delete(destructor_01402180, object);
}

extern "C" void bb_engine_destroys_with_0x010e7ca0_then_deletes(void *object)
{
    destroy_then_delete(destructor_010e7ca0, object);
}

extern "C" void bb_engine_destroys_with_0x01401dc0_then_deletes(void *object)
{
    destroy_then_delete(destructor_01401dc0, object);
}

extern "C" void bb_engine_destroys_with_0x01640e70_then_deletes(void *object)
{
    destroy_then_delete(destructor_01640e70, object);
}

extern "C" void bb_engine_destroys_with_0x0139c4f0_then_deletes(void *object)
{
    destroy_then_delete(destructor_0139c4f0, object);
}

extern "C" uint64_t bb_engine_destroys_with_0x007164e0_then_frees(void *object)
{
    return destroy_then_free(destructor_007164e0, object);
}

extern "C" uint64_t bb_engine_destroys_with_0x005696e0_then_frees(void *object)
{
    return destroy_then_free(destructor_005696e0, object);
}

extern "C" uint64_t bb_engine_destroys_with_0x006fa840_then_frees(void *object)
{
    return destroy_then_free(destructor_006fa840, object);
}

extern "C" uint64_t bb_engine_destroys_with_0x0045a480_then_frees(void *object)
{
    return destroy_then_free(destructor_0045a480, object);
}
