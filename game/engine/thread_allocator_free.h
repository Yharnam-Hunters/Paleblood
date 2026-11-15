// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's deleting functions that hand an object back to the thread's allocator
// (frees_object_through_thread_allocator in symbols/families.csv). The thread-local slot at the
// offset 0x057e40f8 holds points at a holder whose allocator (+0x58) has a sized free in slot
// 0x18; game/loading/fios.h reaches the same allocator.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"
#include "thread.h"

namespace engine::thread_allocator_free {

RT_GLOBAL(0x057e40f8, thread_allocator_offset, const int64_t);

// The header these engine objects share after their vtable: a count, then the allocation size,
// where size_default means the class's own size.
struct ObjectHeader {
    uint16_t count;
    uint16_t allocation_size;
};

struct HeadedObject {
    const void *vtable;
    ObjectHeader header;
};
static_assert(offsetof(HeadedObject, header) == 8);

constexpr uint16_t size_default = 0xffff;

struct ThreadAllocator;
using SizedFree = uint64_t (*)(ThreadAllocator *allocator, void *object, uint64_t size);

struct ThreadAllocator {
    const SizedFree *vtable;
};
constexpr std::size_t sized_free_slot = 0x18;

struct ThreadAllocatorHolder {
    uint8_t unknown_0x0[0x58];
    ThreadAllocator *allocator;
};
static_assert(offsetof(ThreadAllocatorHolder, allocator) == 0x58);

// The sized free of the thread's allocator. The size is passed as the original's edx: 32 bits,
// zero-extended.
inline uint64_t free_sized(void *object, uint32_t size)
{
    auto *holder = *reinterpret_cast<ThreadAllocatorHolder **>(thread_pointer() + thread_allocator_offset.get());
    ThreadAllocator *allocator = holder->allocator;
    const SizedFree free = allocator->vtable[sized_free_slot / sizeof(SizedFree)];
    return free(allocator, object, size);
}

// The object's allocation size, or CLASS_SIZE when it says size_default.
inline uint64_t free_object(void *object, uint32_t class_size)
{
    const uint16_t size = static_cast<const HeadedObject *>(object)->header.allocation_size;
    return free_sized(object, size != size_default ? size : class_size);
}

}  // namespace engine::thread_allocator_free
