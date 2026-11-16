// SPDX-License-Identifier: GPL-2.0-or-later
// The family calls_virtual_on_object_found_by_0x02c4f560 (symbols/families.csv): copies of one
// engine function that differ only in the virtual slot they call. Each finds the object a handle
// names in a context (0x02c4f560, given the handle's kind and index), then tail-calls the object's
// slot K with the object and the context. The finder's result is not checked.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace engine::found_object_call {

// A handle: the kind of table its object lives in and its index there.
struct Handle {
    uint8_t unknown_0x0[0x8];
    uint16_t kind;
    int16_t index;
};
static_assert(offsetof(Handle, kind) == 0x8);
static_assert(offsetof(Handle, index) == 0xa);

// The kind goes zero-extended and the index sign-extended to 32 bits, as the original's esi and
// edx (the upper halves zero).
RT_ORIGINAL(0x02c4f560, find_object, void *(void *context, uint32_t kind, uint32_t index));

using Virtual = uint64_t (*)(void *object, void *context);

inline uint64_t call_virtual(const Handle *handle, void *context, uint32_t slot_offset)
{
    const int32_t index = handle->index;
    void *object = find_object(context, handle->kind, static_cast<uint32_t>(index));
    const uint8_t *table = *static_cast<const uint8_t *const *>(object);
    const Virtual function = *reinterpret_cast<const Virtual *>(table + slot_offset);
    return function(object, context);
}

}  // namespace engine::found_object_call
