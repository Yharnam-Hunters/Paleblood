// SPDX-License-Identifier: GPL-2.0-or-later
// More copies of the engine array's reallocation to a new capacity for 8-byte elements
// (engine/array_reserve.h): 94 copies of the same code as array64_reserve.cpp's, which no
// push_back copy of engine/array64_push_back.cpp calls. Which arrays they serve is not traced
// here; their elements are taken as 64-bit words.
#include <cstdint>

#include "array_reserve.h"
#include "container.h"

namespace {

using Array64 = engine::Array<uint64_t>;

}  // namespace

extern "C" void bb_engine_array64_reserve_0167e800(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016bab60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016d3b10(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016d6d40(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016d6e40(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016e6170(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0179caa0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017d9e20(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017db000(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017f84f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017f85f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017f86f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017f87f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_017f88f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_018616d0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01868f80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01876390(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_018ad900(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_018c6dc0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_018ca6f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01905060(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01922130(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0193ee60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_019b7470(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01b91a30(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01c0be00(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01c47250(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01d1eb10(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01da56c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01e80060(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01f98970(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_020b71c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0210cc10(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0218b320(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_021b8bb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_021b8cb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_021b8db0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_021b8eb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02248840(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02255c50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_022cc6c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0236b0c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0237de00(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0242f260(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0242fda0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_024bcbd0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_024bccd0(Array64 *array, uint64_t capacity)
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

extern "C" void bb_engine_array64_reserve_024c3a50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025575e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025576e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025577e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025578e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025579e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0258c2c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0259e120(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0259e220(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025ae6a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025ae7a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025ae8a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025ae9a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025aeaa0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025aeba0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025bd480(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025c4730(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025c7d80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025c7e80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0260fcf0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02675c30(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02675d30(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026794a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026799f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026829b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02682ab0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0268d9f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0268daf0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026bf750(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026c06c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026c09c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026f4b50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026f4d50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02709d80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02717cd0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a6b420(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ade840(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b48390(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b52c90(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bbda20(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc7690(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c04bb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c34000(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d2dbb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02f22a20(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}
