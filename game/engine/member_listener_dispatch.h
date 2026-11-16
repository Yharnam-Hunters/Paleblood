// SPDX-License-Identifier: GPL-2.0-or-later
// The family calls_0x005c9770_on_member_unless_self (symbols/families.csv): copies of one
// Scaleform function that differ only in the offset of a member. Unless its third argument is the
// object itself (then true), it returns what 0x005c9770 gives for the member at +K with the
// second and third arguments: the dispatch to the member's listeners, true when one handled it.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace engine::member_listener_dispatch {

RT_ORIGINAL(0x005c9770, dispatch_to_listeners, bool(void *listeners, void *first, void *second));

inline bool dispatch_unless_self(void *object, void *first, void *second, uint32_t member_offset)
{
    if (object == second)
        return true;
    return dispatch_to_listeners(static_cast<uint8_t *>(object) + member_offset, first, second);
}

}  // namespace engine::member_listener_dispatch
