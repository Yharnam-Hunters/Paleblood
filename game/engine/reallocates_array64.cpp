// SPDX-License-Identifier: GPL-2.0-or-later
// The family reallocates_array64 (symbols/families.csv): the copies of the engine array
// reallocation for 8-byte elements (engine/array_reserve.h) that engine/array64_reserve.cpp does
// not name one by one. Every copy is the same code; their elements are taken as 64-bit words.
#include <cstdint>

#include "array_reserve.h"
#include "container.h"

extern "C" void bb_engine_reallocates_array64(engine::Array<uint64_t> *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}
