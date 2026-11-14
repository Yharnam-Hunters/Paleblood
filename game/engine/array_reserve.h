// SPDX-License-Identifier: GPL-2.0-or-later
// The engine arrays' reallocation to a new capacity (container.h's Array), which the game compiles
// once per array: a buffer of the capacity from the array's own allocator, the elements copied
// over, the old buffer freed and the three pointers set. The capacity is taken as given, so one
// below the count leaves the end past the capacity end, as in the game.
#pragma once

#include <cstdint>

#include "container.h"
#include "transaction.h"

namespace engine {

template <typename T> void array_reserve(Array<T> *array, uint64_t capacity)
{
    auto *buffer = static_cast<T *>(array->allocator->allocate(capacity * sizeof(T), alignof(T)));
    if (!buffer) report_failed_allocation();
    // The elements are copy-constructed in place, each placement checking its own address: with a
    // null buffer the first is skipped and the next goes to address sizeof(T) (the game faults
    // there).
    uintptr_t to = reinterpret_cast<uintptr_t>(buffer);
    for (const T *from = array->begin; from != array->end; ++from, to += sizeof(T))
        if (to) *reinterpret_cast<T *>(to) = *from;
    T *const begin = array->begin;
    const uint64_t count = array->count();
    if (begin) array->allocator->free(begin);
    // Address arithmetic, as the original: the buffer may be null here.
    const uintptr_t base = reinterpret_cast<uintptr_t>(buffer);
    array->capacity_end = reinterpret_cast<T *>(base + capacity * sizeof(T));
    array->end = reinterpret_cast<T *>(base + count * sizeof(T));
    array->begin = buffer;
}

}  // namespace engine
