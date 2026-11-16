// SPDX-License-Identifier: GPL-2.0-or-later
// The family calls_delegate_at_0x18_else_virtual_0x80 (symbols/families.csv): copies of one
// engine function. When the delegate the object holds is set, its function (an Itanium pointer to
// member function at +0x30, with the adjustment at +0x38, bound to the object at +0x18) gets the
// second argument and the context dword at +0x40; else slot 0x80 of the second argument gets
// the fixed address 0x04dc0016. Both are tail calls.
#include <cstddef>
#include <cstdint>

#include "runtime/guest.h"

namespace {

struct DelegateAt0x18 {
    uint8_t unknown_0x0[0x18];
    uint8_t *object;
    uint8_t unknown_0x20[0x10];
    uintptr_t function;
    intptr_t adjustment;
    uint32_t context;
};
static_assert(offsetof(DelegateAt0x18, object) == 0x18);
static_assert(offsetof(DelegateAt0x18, function) == 0x30);
static_assert(offsetof(DelegateAt0x18, adjustment) == 0x38);
static_assert(offsetof(DelegateAt0x18, context) == 0x40);

// The context is passed as the original's edx: 32 bits, zero-extended.
using Handler = uint64_t (*)(void *object, void *argument, uint64_t context);
using FallbackFunction = uint64_t (*)(void *argument, const void *address);

constexpr uintptr_t virtual_flag = 1;   // the function is 1 + an offset into the object's table
constexpr std::size_t fallback_slot = 0x80;
constexpr uint32_t fallback_address = 0x04dc0016;

}  // namespace

extern "C" uint64_t bb_engine_calls_delegate_at_0x18_else_virtual_0x80(void *owner, void *argument)
{
    const auto *delegate = static_cast<const DelegateAt0x18 *>(owner);
    if (delegate->function == 0) {
        const uint8_t *table = *static_cast<uint8_t *const *>(argument);
        const auto fallback = *reinterpret_cast<const FallbackFunction *>(table + fallback_slot);
        return fallback(argument, rt_guest(fallback_address));
    }
    uint8_t *object = delegate->object + delegate->adjustment;
    Handler handler;
    if (delegate->function & virtual_flag) {
        const uint8_t *table = *reinterpret_cast<uint8_t *const *>(object);
        handler = *reinterpret_cast<const Handler *>(table + delegate->function - virtual_flag);
    } else {
        handler = reinterpret_cast<Handler>(delegate->function);
    }
    return handler(object, argument, delegate->context);
}
