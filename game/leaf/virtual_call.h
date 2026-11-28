// SPDX-License-Identifier: GPL-2.0-or-later
// What a forwarding thunk knows about the objects it calls: the first qword of each points at a
// table of virtual functions (game/leaf/forwarding.h has how the thunks pass their arguments on).
#ifndef GAME_LEAF_VIRTUAL_CALL_H
#define GAME_LEAF_VIRTUAL_CALL_H

#include <cstddef>
#include <cstdint>

#include "forwarding.h"

// The function in the slot SlotOffset bytes into the object's virtual table, as a Function (a
// slot whose callee takes fewer arguments is read with its own type).
template <std::size_t SlotOffset, typename Function = ForwardedFunction>
Function virtual_function(const void *object)
{
    static_assert(SlotOffset % sizeof(Function) == 0, "a slot is a whole function pointer");
    const Function *virtual_table = *static_cast<const Function *const *>(object);
    return virtual_table[SlotOffset / sizeof(Function)];
}

// The function EntryOffset bytes into a table of functions.
template <std::size_t EntryOffset, typename Function = ForwardedFunction>
Function table_entry(const void *table)
{
    static_assert(EntryOffset % sizeof(Function) == 0, "an entry is a whole function pointer");
    return static_cast<const Function *>(table)[EntryOffset / sizeof(Function)];
}

// The pointer Offset bytes into the object.
template <std::size_t Offset>
void *member_at(const void *object)
{
    return *reinterpret_cast<void *const *>(static_cast<const uint8_t *>(object) + Offset);
}

#endif
