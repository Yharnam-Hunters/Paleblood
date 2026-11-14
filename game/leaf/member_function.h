// SPDX-License-Identifier: GPL-2.0-or-later
// Pointers to member functions as the delegate families (game/leaf/delegates.cpp) hold them, in
// the Itanium C++ ABI layout: the function's address, or for a virtual function one plus the
// offset of its slot in the object's virtual table (an odd number), followed by the adjustment
// added to the object pointer before the call.
#ifndef GAME_LEAF_MEMBER_FUNCTION_H
#define GAME_LEAF_MEMBER_FUNCTION_H

#include <cstddef>
#include <cstdint>

#include "forwarding.h"

struct MemberFunctionPointer {
    uintptr_t function;
    intptr_t adjustment;
};

constexpr uintptr_t virtual_member_function = 1;  // the low bit of a virtual one's function field

struct BoundMethod {
    void *target;
    ForwardedFunction function;
};

// The method on the object: the object plus the adjustment, and the function to call on it.
inline BoundMethod bind(void *object, const MemberFunctionPointer &method)
{
    void *target = static_cast<uint8_t *>(object) + method.adjustment;
    if ((method.function & virtual_member_function) == 0)
        return {target, reinterpret_cast<ForwardedFunction>(method.function)};
    const uint8_t *virtual_table = *static_cast<const uint8_t *const *>(target);
    const std::size_t slot_offset = method.function - virtual_member_function;
    return {target, *reinterpret_cast<const ForwardedFunction *>(virtual_table + slot_offset)};
}

#endif
