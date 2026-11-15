// SPDX-License-Identifier: GPL-2.0-or-later
// Two families of deleting destructors (symbols/families.csv), each the copies of one function:
//  - destroys_then_deletes: the object's destructor is one shared function, called before the
//    object goes to operator delete;
//  - calls_virtual_0x28_then_deletes_if_asked: the object's destructor is slot 0x28 of its
//    table, and the object goes to operator delete only when the second argument's low byte is
//    set.
// Both end in a tail call of operator delete.
#include <cstddef>
#include <cstdint>

#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x024b5a60, shared_destructor, void(void *object));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

using VirtualDestructor = void (*)(void *object);
constexpr std::size_t destructor_slot = 0x28;

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
