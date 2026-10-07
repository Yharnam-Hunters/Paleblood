// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's heap allocators: objects with a vtable whose slots allocate and free.
#pragma once

#include <cstddef>
#include <cstdint>

namespace engine {

struct Allocator;
// What the info slot fills in: flags, of which this one says the heap is valid.
struct AllocatorInfo {
    uint8_t flags;
    uint8_t unknown_0x01[0xf];
};
constexpr uint8_t allocator_info_valid = 0x20;

struct AllocatorVtable {
    void *unknown_slots[2];
    void *(*allocate_small)(Allocator *, uint64_t size);
    void *unknown_slots_1[1];
    void (*info)(AllocatorInfo *out, Allocator *, int32_t);
    void *unknown_slots_2[5];
    void *(*allocate_default)(Allocator *, uint64_t size);
    void *(*allocate)(Allocator *, uint64_t size, uint64_t alignment);
    void *unknown_slots_3[2];
    void (*free)(Allocator *, void *data);
};
static_assert(offsetof(AllocatorVtable, allocate_small) == 0x10 && offsetof(AllocatorVtable, info) == 0x20);
static_assert(offsetof(AllocatorVtable, allocate_default) == 0x50 && offsetof(AllocatorVtable, allocate) == 0x58);
static_assert(offsetof(AllocatorVtable, free) == 0x70);

struct Allocator {
    const AllocatorVtable *vtable;

    void *allocate(uint64_t size, uint64_t alignment) { return vtable->allocate(this, size, alignment); }
    void free(void *data) { vtable->free(this, data); }
};

}  // namespace engine
