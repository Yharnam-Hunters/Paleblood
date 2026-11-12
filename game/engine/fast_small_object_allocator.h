// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's fast small object allocators (Dantelion2 DLFastSmallObjectAllocator): one fast
// fixed-size allocator (fast_fixed_allocator.h) per size class, in an array from a backing
// allocator.
//
// Two variants share the layout: a plain one, whose lock is a placeholder object, and a locked
// one, which owns a mutex (pthread_mutex.h). Their constructors are 0x02fa1640 and 0x02fa1950.
#pragma once

#include <cstddef>
#include <cstdint>

#include "fast_fixed_allocator.h"
#include "fixed_allocator.h"
#include "pthread_mutex.h"

namespace engine::fast_small_object_allocator {

using fixed_allocator::BackingAllocator;

struct Allocator {
    const void *vtable;
    fast_fixed_allocator::Allocator *classes;
    uint64_t class_count;
    uint64_t granularity_shift; // log2 of the step between class sizes
    uint64_t largest;           // the largest object size, rounded up to the alignment
    BackingAllocator *backing;  // where the class array came from
};
static_assert(offsetof(Allocator, class_count) == 0x10 && offsetof(Allocator, backing) == 0x28);
static_assert(sizeof(Allocator) == 0x30);

struct Locked {
    Allocator base;
    pthread_mutex::Mutex lock;
};
static_assert(offsetof(Locked, lock) == 0x30);

// The vtables the destructors set first (their address points).
RT_GLOBAL(0x0575b630, plain_vtable, const uint8_t);
RT_GLOBAL(0x0575b650, locked_vtable, const uint8_t);
// A fast fixed-size allocator's destructor body (fast_fixed_allocator_release.cpp).
RT_ORIGINAL(0x02a3bc40, release_class, void(fast_fixed_allocator::Allocator *));
// The executable's call stub for operator delete.
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// What both variants' destructors do: release every class's allocator, then give the class array
// back to the backing allocator. The array pointer is cleared afterwards unless the object is
// being deleted.
inline void destroy_classes(Allocator *allocator, bool clear_array = true)
{
    for (uint64_t index = 0; index < allocator->class_count; ++index)
        release_class(allocator->classes + index);
    allocator->backing->vtable->free(allocator->backing, allocator->classes);
    if (clear_array)
        allocator->classes = nullptr;
}

}  // namespace engine::fast_small_object_allocator
