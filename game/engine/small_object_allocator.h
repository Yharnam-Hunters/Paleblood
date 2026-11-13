// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's small object allocators (Dantelion2 DLSmallObjectAllocator): one fixed-size
// allocator (fixed_allocator.h) per size class, from the smallest object size up to the largest
// in steps of a granularity, the last class holding the largest size.
//
// Two variants share the layout of their size classes: a plain one, whose lock is a placeholder
// object, and a locked one, which owns a mutex.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "fixed_allocator.h"
#include "fixed_allocator_initialize.h"
#include "pthread_mutex.h"

namespace engine::small_object_allocator {

using fixed_allocator::BackingAllocator;

// How the sizes are split into classes.
struct SizeClasses {
    uint64_t chunk_size;        // the room each class's chunks give their elements
    uint64_t largest;           // the largest object size, rounded up to 8: the last class
    uint64_t smallest;          // the smallest, rounded up to the granularity: the first class
    uint64_t granularity;       // a power of two, at least 8
    uint64_t class_count;
    uint64_t granularity_shift; // log2 of the granularity
    BackingAllocator *backing;
};
static_assert(sizeof(SizeClasses) == 0x38);

// The lock of the locked variant: the engine's mutex object (pthread_mutex.h).
using pthread_mutex::Mutex;

struct Plain {
    const void *vtable;
    fixed_allocator::Allocator *classes;
    const void *lock_vtable;    // a lock that does nothing
    SizeClasses sizes;
};
static_assert(offsetof(Plain, lock_vtable) == 0x10 && offsetof(Plain, sizes) == 0x18 && sizeof(Plain) == 0x50);

struct Locked {
    const void *vtable;
    fixed_allocator::Allocator *classes;
    Mutex lock;
    SizeClasses sizes;
};
static_assert(offsetof(Locked, lock) == 0x10 && offsetof(Locked, sizes) == 0x28 && sizeof(Locked) == 0x60);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLSmallObjectAllocator.cpp"
RT_GLOBAL(0x04d3cc7e, source_file, const char);
// "chunk size is too small. ( chunkSize %d maxObjectSize %d)"
RT_GLOBAL(0x04d3cceb, message_chunk_too_small, const char);
constexpr int32_t line_chunk_too_small = 0xbe;

// A vtable whose address points 0x10 into its object (past the offset-to-top and type-info words).
inline const void *vtable_entries(const uint8_t *vtable_object)
{
    constexpr std::ptrdiff_t entries_offset = 0x10;
    return vtable_object + entries_offset;
}

// A size class's alignment: the lowest set bit of its size, as the game computes it (in 32 bits,
// sign-extended).
inline uint64_t class_alignment(uint64_t size)
{
    const uint32_t low = static_cast<uint32_t>(size);
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(low & (0u - low))));
}

// Splits the sizes into classes and sets up one fixed-size allocator per class in an array from
// the backing allocator. The chunk size must exceed twice the largest object size (reported
// otherwise; the report returns). The granularity is 16 unless the one given is a power of two,
// and at least 8. Classes run from the smallest size rounded up to the granularity in steps of it,
// and a last class takes the largest size rounded up to 8; each class's allocator gets the class
// size and its alignment, with the fixed allocator's checks.
inline void set_up_classes(fixed_allocator::Allocator **classes, SizeClasses *sizes, BackingAllocator *backing,
                           uint64_t chunk_size, uint64_t largest, uint64_t smallest, uint64_t granularity)
{
    constexpr uint64_t default_granularity = 16;
    constexpr uint64_t smallest_granularity = 8;
    constexpr uint64_t size_rounding = 8;
    constexpr uint64_t classes_alignment = 8;
    constexpr uint64_t largest_objects_per_chunk = 2;  // the chunk must hold more than two of the largest
    sizes->chunk_size = chunk_size;
    sizes->backing = backing;
    if (chunk_size <= largest * largest_objects_per_chunk)
        engine::fatal_error(source_file.address(), line_chunk_too_small, message_chunk_too_small.address(), chunk_size,
                            largest);
    uint64_t step = (granularity & (granularity - 1)) == 0 ? granularity : default_granularity;
    if (step < smallest_granularity)
        step = smallest_granularity;
    sizes->granularity = step;
    const uint64_t shift = 63 - __builtin_clzll(step);
    sizes->granularity_shift = shift;
    const uint64_t first = (smallest + step - 1) & (0 - step);
    sizes->smallest = first;
    const uint64_t last = (largest + size_rounding - 1) & ~(size_rounding - 1);
    sizes->largest = last;
    const uint64_t stepped = (last - 1 + step - first) >> shift;
    sizes->class_count = stepped + 1;
    *classes = static_cast<fixed_allocator::Allocator *>(backing->vtable->allocate_aligned(
        backing, (stepped + 1) * sizeof(fixed_allocator::Allocator), classes_alignment));

    // (Each slot is built in place, and building in place skips a null address: with no array,
    // only the first slot is skipped.)
    uint64_t size = first;
    for (uint64_t index = 0; index < stepped; ++index, size += step) {
        fixed_allocator::Allocator *slot = *classes + index;
        if (slot)
            fixed_allocator::initialize_checked(slot, backing, size, chunk_size, class_alignment(size));
    }
    fixed_allocator::Allocator *slot = *classes + stepped;
    if (slot)
        fixed_allocator::initialize_checked(slot, backing, last, chunk_size, class_alignment(last));
}

}  // namespace engine::small_object_allocator
