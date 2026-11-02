// SPDX-License-Identifier: GPL-2.0-or-later
// The effect collector keeps an opaque storage pointer in its owner. These offsets
// describe only the fields this resize helper accesses, not a complete owner type.
#pragma once

#include "allocator.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace engine::cache {

struct StorageOwner {
    uint8_t unknown_0x0000[0x8];
    Allocator *allocator;
    uint8_t unknown_0x0010[0x160];
    void *storage;
};
static_assert(offsetof(StorageOwner, allocator) == 0x8);
static_assert(offsetof(StorageOwner, storage) == 0x170);
static_assert(sizeof(StorageOwner) == 0x178);

constexpr uint64_t storage_alignment = 16;

// Native scalar loads accept unaligned fields. Use the layout only for named
// offsets; no typed lvalue or no-alias promise is imposed on the supplied views.
template <typename Field>
Field read_field(const void *object, size_t member_offset)
{
    Field value;
    const auto *bytes = static_cast<const uint8_t *>(object);
    std::memcpy(&value, bytes + member_offset, sizeof(value));
    return value;
}

inline void *read_storage(const void *owner)
{
    return read_field<void *>(owner, offsetof(StorageOwner, storage));
}

inline Allocator *read_allocator(const void *owner)
{
    return read_field<Allocator *>(owner, offsetof(StorageOwner, allocator));
}

inline void publish_storage(void *owner, void *storage)
{
    auto *bytes = static_cast<uint8_t *>(owner);
    std::memcpy(bytes + offsetof(StorageOwner, storage), &storage, sizeof(storage));
}

inline auto release_method(const Allocator *allocator)
{
    const auto *vtable = read_field<const AllocatorVtable *>(allocator, offsetof(Allocator, vtable));
    return read_field<decltype(AllocatorVtable::free)>(vtable, offsetof(AllocatorVtable, free));
}

inline auto allocation_method(const Allocator *allocator)
{
    const auto *vtable = read_field<const AllocatorVtable *>(allocator, offsetof(Allocator, vtable));
    return read_field<decltype(AllocatorVtable::allocate)>(vtable, offsetof(AllocatorVtable, allocate));
}

}  // namespace engine::cache
