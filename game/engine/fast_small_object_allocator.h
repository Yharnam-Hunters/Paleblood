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

struct Plain {
    Allocator base;
    const void *lock_vtable;    // a lock that does nothing
};
static_assert(offsetof(Plain, lock_vtable) == 0x30);

struct Locked {
    Allocator base;
    pthread_mutex::Mutex lock;
};
static_assert(offsetof(Locked, lock) == 0x30);

// The vtables (their address points).
RT_GLOBAL(0x0575b630, plain_vtable, const uint8_t);
RT_GLOBAL(0x0575b650, locked_vtable, const uint8_t);
// A fast fixed-size allocator's destructor body (fast_fixed_allocator_release.cpp).
RT_ORIGINAL(0x02a3bc40, release_class, void(fast_fixed_allocator::Allocator *));
// The executable's call stub for operator delete.
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLFastSmallObjectAllocator.cpp"
RT_GLOBAL(0x04d3c3da, source_file, const char);
// "min obj size(%d) > max obj size(%d)"
RT_GLOBAL(0x04d3c44b, message_smallest_too_large, const char);
// "objStepSize(%d) is not a power of two." (in Japanese)
RT_GLOBAL(0x04d3c46f, message_step_not_power_of_two, const char);
// "maxAlign(%d) is not a power of two." (in Japanese)
RT_GLOBAL(0x04d3c4a6, message_alignment_not_power_of_two, const char);
// "Could not allocate the fixed-size allocators' buffer (%d bytes). heap id = %d free size = %d"
// (in Japanese)
RT_GLOBAL(0x04d3c4da, message_no_class_array, const char);
constexpr int32_t line_smallest_too_large = 0x76, line_step_not_power_of_two = 0x77,
                  line_alignment_not_power_of_two = 0x78, line_no_class_array = 0x9a;

// A fast fixed-size allocator's set-up (fast_fixed_allocator_initialize.cpp).
RT_ORIGINAL(0x02a3baa0, initialize_class,
            void(fast_fixed_allocator::Allocator *, BackingAllocator *, uint64_t block_size, uint64_t chunk_size,
                 uint64_t alignment));

// The backing allocator's queries for the failure report: its heap id and its free size.
struct BackingQueries {
    void *unknown_slots[2];
    int32_t (*heap_id)(BackingAllocator *);
    void *unknown_slots_0x18[3];
    uint64_t (*free_size)(BackingAllocator *);
};
static_assert(offsetof(BackingQueries, heap_id) == 0x10 && offsetof(BackingQueries, free_size) == 0x30);

inline const BackingQueries *queries(BackingAllocator *backing)
{
    return reinterpret_cast<const BackingQueries *>(backing->vtable);
}

// What both variants' constructors do after their own fields: checks the sizes (reports return;
// the set-up goes on), splits them into classes and sets up one fast fixed-size allocator per
// class in an array from the backing allocator. The step and the alignment are at least 8. The
// classes run from the largest size, rounded up to the alignment, down in steps; their count
// reaches down to the smallest size rounded down to 8 (at least 8). Each class's allocator gets
// its size, the chunk size and the lowest set bit of its size as its alignment (computed in 32
// bits, sign-extended). A failed array allocation is reported with the backing allocator's heap id
// and free size; the slots are still visited, and only the null one is skipped.
inline void set_up_classes(Allocator *allocator, BackingAllocator *backing, uint64_t chunk_size, uint64_t largest,
                           uint64_t smallest, uint64_t step, uint64_t max_alignment)
{
    constexpr uint64_t smallest_step = 8;
    constexpr uint64_t smallest_alignment = 8;
    constexpr uint64_t class_array_alignment = 8;
    if (smallest > largest)
        fatal_error(source_file.address(), line_smallest_too_large, message_smallest_too_large.address(), smallest,
                    largest);
    if (step & (step - 1))
        fatal_error(source_file.address(), line_step_not_power_of_two, message_step_not_power_of_two.address(),
                    step);
    if (max_alignment & (max_alignment - 1))
        fatal_error(source_file.address(), line_alignment_not_power_of_two,
                    message_alignment_not_power_of_two.address(), max_alignment);
    const uint64_t class_step = step > smallest_step ? step : smallest_step;
    const uint64_t alignment = max_alignment > smallest_alignment ? max_alignment : smallest_alignment;
    const uint64_t shift = 63 - __builtin_clzll(class_step);
    allocator->granularity_shift = shift;
    const uint64_t top_size = (largest + alignment - 1) & (0 - alignment);
    allocator->largest = top_size;
    const uint64_t bottom_size = smallest < smallest_step ? smallest_step : smallest & ~(smallest_step - 1);
    const uint64_t count = ((top_size - bottom_size) >> shift) + 1;
    allocator->class_count = count;
    allocator->classes = static_cast<fast_fixed_allocator::Allocator *>(backing->vtable->allocate_aligned(
        backing, count * sizeof(fast_fixed_allocator::Allocator), class_array_alignment));
    if (!allocator->classes) {
        const uint64_t array_bytes = allocator->class_count * sizeof(fast_fixed_allocator::Allocator);
        const int32_t heap_id = queries(backing)->heap_id(backing);
        fatal_error(source_file.address(), line_no_class_array, message_no_class_array.address(), array_bytes,
                    heap_id, queries(backing)->free_size(backing));
    }
    uint64_t size = top_size;
    for (uint64_t index = 0; index < count; ++index, size -= class_step) {
        fast_fixed_allocator::Allocator *slot = allocator->classes + index;
        if (slot) {
            const uint32_t low = static_cast<uint32_t>(size);
            const int32_t lowest_bit = static_cast<int32_t>(low & (0u - low));
            initialize_class(slot, backing, size, chunk_size, static_cast<uint64_t>(static_cast<int64_t>(lowest_bit)));
        }
    }
}

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
