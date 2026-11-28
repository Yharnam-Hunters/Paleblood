// SPDX-License-Identifier: GPL-2.0-or-later
// Three engine array families (symbols/families.csv, docs/FAMILIES.md), each the copies of one
// array's reallocation that the game compiles once per array (engine/array_reserve.h):
// - reallocates_array_of_4_byte_elements: to a given capacity, 4-byte elements (the reallocation
//   engine/array32_push_back.cpp's copies call, for arrays it does not name);
// - reallocates_array_of_12_byte_elements: the same for 12-byte elements;
// - grows_full_array_of_16_byte_elements: when the array is full, the growth push_back makes
//   (engine/array_push_back.h: to array_grown_capacity's capacity), for 16-byte elements; an array
//   with room is left alone.
// Which arrays they serve is not traced here. The element types follow the code: the 4-byte
// elements move as one dword with 4-byte alignment, the 12-byte ones as a qword and a dword with
// 4-byte alignment, the 16-byte ones as two qwords with 8-byte alignment.
#include <cstdint>

#include "array_reserve.h"
#include "container.h"

namespace {

using Array32 = engine::Array<uint32_t>;

struct Element12 {
    uint32_t word_0x0;
    uint32_t word_0x4;
    uint32_t word_0x8;
};
using Array12 = engine::Array<Element12>;

struct Element16 {
    uint64_t word_0x0;
    uint64_t word_0x8;
};
using Array16 = engine::Array<Element16>;

}  // namespace

extern "C" void bb_engine_reallocates_array_of_4_byte_elements(Array32 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_reallocates_array_of_12_byte_elements(Array12 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_grows_full_array_of_16_byte_elements(Array16 *array)
{
    if (array->end == array->capacity_end)
        engine::array_reserve(array, engine::array_grown_capacity<Element16>(array->count()));
}
