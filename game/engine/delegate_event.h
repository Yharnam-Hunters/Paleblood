// SPDX-License-Identifier: GPL-2.0-or-later
// Engine objects that post an event to the delegate they hold at +0x50
// (posts_event_to_delegate_at_0x50 in symbols/families.csv). The delegate is an Itanium pointer
// to member function (+0x58: the function, or 1 + its virtual table offset; +0x60: the
// adjustment) bound to an object (+0x50), with a context dword (+0x68) passed as the third
// argument. The event is built on the caller's stack.
#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::delegate_event {

struct Event {
    uint32_t kind;
    uint32_t value;
    uint64_t data;
};
static_assert(sizeof(Event) == 0x10);

// The context goes as the original's edx: 32 bits, zero-extended.
struct Delegate {
    uint8_t *object;
    uintptr_t function;
    intptr_t adjustment;
    uint32_t context;
};

struct Poster {
    uint8_t unknown_0x0[0x50];
    Delegate delegate;
};
static_assert(offsetof(Poster, delegate) == 0x50);
static_assert(offsetof(Poster, delegate.context) == 0x68);

constexpr uintptr_t virtual_flag = 1;   // the function is 1 + an offset into the object's table

// Call the delegate with EVENT (whatever the caller built) and its context.
inline void invoke(const Delegate &delegate, const void *event)
{
    using AnyHandler = void (*)(void *object, const void *event, uint64_t context);
    uint8_t *object = delegate.object + delegate.adjustment;
    AnyHandler handler;
    if (delegate.function & virtual_flag) {
        const uint8_t *table = *reinterpret_cast<uint8_t *const *>(object);
        handler = *reinterpret_cast<const AnyHandler *>(table + delegate.function - virtual_flag);
    } else {
        handler = reinterpret_cast<AnyHandler>(delegate.function);
    }
    handler(object, event, delegate.context);
}

inline void post(void *poster, uint32_t kind, uint64_t value, uint64_t data)
{
    const Delegate &delegate = static_cast<Poster *>(poster)->delegate;
    if (delegate.function == 0)
        return;
    // The value is the low 32 bits (the text event's originals leave it unset; it is 0 here).
    const Event event{kind, static_cast<uint32_t>(value), data};
    invoke(delegate, &event);
}

}  // namespace engine::delegate_event
