// SPDX-License-Identifier: GPL-2.0-or-later
// More copies of the engine array's reallocation to a new capacity for 8-byte elements
// (engine/array_reserve.h): 7 copies of the same code as array64_reserve.cpp's, which no
// push_back copy of engine/array64_push_back.cpp calls. Code follows each of them before the
// next function, so the reallocates_array64 family cannot hold them. Which arrays they serve
// is not traced here; their elements are taken as 64-bit words.
#include <cstdint>

#include "array_reserve.h"
#include "container.h"

namespace {

using Array64 = engine::Array<uint64_t>;

}  // namespace

extern "C" void bb_engine_array64_reserve_017db000(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01922130(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_020b71c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_024bcdd0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_024c3160(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025c7e80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a6b420(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}
