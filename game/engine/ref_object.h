// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's reference-counted objects (DLReferenceCountObject): taking and dropping a reference.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace engine {

// A reference-counted object (DLReferenceCountObject): the last reference destroys it through its
// first vtable slot.
struct RefObject;
struct RefObjectVtable {
    void (*destroy)(RefObject *);
};
struct RefObject {
    const RefObjectVtable *vtable;
    int32_t references;
};
static_assert(offsetof(RefObject, references) == 0x8);

// "DLReferenceCountObject: Invalid Unref() call. m_nRefCount is 0 or less"
RT_GLOBAL(0x04d40331, message_invalid_unref, const char);
constexpr int32_t line_invalid_unref = 0x3e;

// Takes a reference (an atomic increment).
inline void retain(RefObject *object)
{
    __atomic_fetch_add(&object->references, 1, __ATOMIC_SEQ_CST);
}

// Drops a reference (an atomic decrement): the last one destroys the object; a count already at
// 0 or below is reported through the fatal error report, which carries on.
inline void release(RefObject *object)
{
    const int32_t before = __atomic_fetch_sub(&object->references, 1, __ATOMIC_SEQ_CST);
    if (before == 1)
        object->vtable->destroy(object);
    else if (before <= 0)
        fatal_error(nullptr, line_invalid_unref, message_invalid_unref.address());
}

}  // namespace engine
