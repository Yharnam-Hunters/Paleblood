// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's growable array (an MSVC-style vector that carries its allocator) and its growth rule.
#pragma once

#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "engine.h"
#include "static_wide_string.h"

namespace engine {

template <typename T> struct Array {
    uint64_t unknown_0x00;
    T *begin;
    T *end;
    T *capacity_end;
    Allocator *allocator;

    uint64_t count() const { return end - begin; }
    bool contains(const T *item) const { return item < end && begin <= item; }
};

// The largest element count an array of T allows (the size type's range over the element size).
template <typename T> constexpr uint64_t array_max_count = ~uint64_t{0} / sizeof(T);

// The capacity a full array grows to: one and a half times its count, at least one more; when
// half again would pass the maximum, just one more.
template <typename T> constexpr uint64_t array_grown_capacity(uint64_t count)
{
    const uint64_t half = count >> 1;
    uint64_t capacity = array_max_count<T> - half >= count ? count + half : 0;
    if (capacity < count + 1)
        capacity = count + 1;
    return capacity;
}

// The alignment the arrays ask their allocator for.
constexpr uint64_t array_buffer_alignment = 8;

// The check a container makes of the heap it is given: one whose info lacks the valid flag is
// reported, and the container is built on it anyway.
inline void check_container_heap(Allocator *heap)
{
    AllocatorInfo info;
    heap->vtable->info(&info, heap, 0);
    if ((info.flags & allocator_info_valid) == 0)
        fatal_error(nullptr, static_wide_string::invalid_heap_line, static_wide_string::invalid_heap_message.address());
}

}  // namespace engine
