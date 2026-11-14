// SPDX-License-Identifier: GPL-2.0-or-later
// The engine arrays' push_back (container.h's Array), which the game compiles once per element
// type and reallocation: a full array first grows to one and a half times its count
// (array_grown_capacity) through its own reallocation. An item inside the array itself is read
// again at the same index of the new buffer. With no buffer after the growth (a failed allocation
// that was only reported), nothing is stored, but the end still moves on by one element, as in
// the game.
#pragma once

#include <cstdint>

#include "container.h"

namespace engine {

template <typename T, typename Grow>
void array_push_back(Array<T> *array, const T *item, Grow grow)
{
    const bool inside_array = array->contains(item);
    if (array->end == array->capacity_end) {
        const uint64_t index = inside_array ? item - array->begin : 0;
        grow(array, array_grown_capacity<T>(array->count()));
        if (inside_array)
            item = array->begin + index;
    }
    if (array->end)
        *array->end = *item;
    array->end += 1;
}

}  // namespace engine
