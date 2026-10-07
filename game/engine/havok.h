// SPDX-License-Identifier: GPL-2.0-or-later
// Havok types the engine embeds (hkArray, hkMemoryAllocator), as laid out in memory.
#pragma once

#include <cstddef>
#include <cstdint>

namespace engine {

// hkArray: data, size, and the capacity with flags in its top bits (the array does not own its
// buffer when the top bit is set).
template <typename T> struct HkArray {
    T *data;
    int32_t size;
    uint32_t capacity_and_flags;
};
constexpr uint32_t hk_array_dont_deallocate = 0x80000000, hk_array_capacity_mask = 0x3fffffff;

inline int32_t hk_capacity(uint32_t capacity_and_flags) { return static_cast<int32_t>(capacity_and_flags & hk_array_capacity_mask); }
inline bool hk_owns_buffer(uint32_t capacity_and_flags) { return static_cast<int32_t>(capacity_and_flags) >= 0; }

// hkMemoryAllocator: buffer allocation (the size may be rounded up and is written back) and free.
struct HkMemoryAllocator;
struct HkMemoryAllocatorVtable {
    void *unknown_slots[4];
    void *(*buf_alloc)(HkMemoryAllocator *, int32_t *bytes);
    void (*buf_free)(HkMemoryAllocator *, void *data, int32_t bytes);
};
static_assert(offsetof(HkMemoryAllocatorVtable, buf_alloc) == 0x20 && offsetof(HkMemoryAllocatorVtable, buf_free) == 0x28);
struct HkMemoryAllocator {
    const HkMemoryAllocatorVtable *vtable;
};

// The header Havok's reference-counted objects get from their constructors.
constexpr uint32_t hk_reference_header = 0xffff0001;

}  // namespace engine
