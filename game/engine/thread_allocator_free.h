// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's deleting functions that hand an object back to the thread's allocator
// (frees_object_through_thread_allocator in symbols/families.csv), and its sized deletes and
// allocations. The thread-local slot at the offset 0x057e40f8 holds points at a holder whose
// allocator (+0x58) has an allocate in slot 0x10 and a sized free in slot 0x18;
// game/loading/fios.h reaches the same allocator.
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
using Allocate = void *(*)(ThreadAllocator *allocator, uint64_t size);
using SizedFree = uint64_t (*)(ThreadAllocator *allocator, void *object, uint64_t size);

struct ThreadAllocatorVtable {
    uint8_t unknown_0x0[0x10];
    Allocate allocate;
    SizedFree free_sized;
};
static_assert(offsetof(ThreadAllocatorVtable, allocate) == 0x10);
static_assert(offsetof(ThreadAllocatorVtable, free_sized) == 0x18);

struct ThreadAllocator {
    const ThreadAllocatorVtable *vtable;
};

struct ThreadAllocatorHolder {
    uint8_t unknown_0x0[0x58];
    ThreadAllocator *allocator;
};
static_assert(offsetof(ThreadAllocatorHolder, allocator) == 0x58);

inline ThreadAllocator *thread_allocator()
{
    auto *holder = *reinterpret_cast<ThreadAllocatorHolder **>(thread_pointer() + thread_allocator_offset.get());
    return holder->allocator;
}

// The sized free of the thread's allocator. The size is passed as the original's edx: 32 bits,
// zero-extended.
inline uint64_t free_sized(void *object, uint32_t size)
{
    ThreadAllocator *allocator = thread_allocator();
    return allocator->vtable->free_sized(allocator, object, size);
}

// A sized delete: nothing for a null pointer.
inline void delete_sized(void *object, uint32_t size)
{
    if (object)
        free_sized(object, size);
}

// The thread's allocator's allocate. The size is passed as the original's esi, zero-extended.
inline void *allocate(uint32_t size)
{
    ThreadAllocator *allocator = thread_allocator();
    return allocator->vtable->allocate(allocator, size);
}

// The object's allocation size, or CLASS_SIZE when it says size_default.
inline uint64_t free_object(void *object, uint32_t class_size)
{
    const uint16_t size = static_cast<const HeadedObject *>(object)->header.allocation_size;
    return free_sized(object, size != size_default ? size : class_size);
}

}  // namespace engine::thread_allocator_free
