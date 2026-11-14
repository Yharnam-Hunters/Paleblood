// SPDX-License-Identifier: GPL-2.0-or-later
// The engine array's reallocation to a new capacity for 8-byte elements (engine/array_reserve.h),
// as the game compiles it for each array: 159 copies of the same code. Most are the reallocations
// that engine/array64_push_back.cpp's copies call. Which arrays they serve is not traced here;
// their elements are taken as 64-bit words.
#include <cstdint>

#include "array_reserve.h"
#include "container.h"

namespace {

using Array64 = engine::Array<uint64_t>;

}  // namespace

extern "C" void bb_engine_array64_reserve_016a5130(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016b1290(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016d1fc0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016d9f70(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_016e1490(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0179c3b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0179fd90(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0187df30(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0189a350(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0189e790(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_018bf2c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01904bb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0193ed60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01c42780(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01c62b70(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_01ceb6a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0215f2f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02255b50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02266eb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02266fb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02273af0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_022b03e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_022b0720(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02463540(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02471310(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02471410(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_024fa650(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_024fa750(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02559480(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_025c4630(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0260fdf0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0265c6a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_0265e210(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02682520(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02686cd0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02686dd0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026c07c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026c08c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_026f4c50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02706ac0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02707280(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_027077a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a46410(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a6e960(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a6f3d0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a76a30(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a78620(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a8e840(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02a930b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02aa3430(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b15c20(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b17760(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b18a60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b1c130(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b220f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b26b10(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b2a280(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b2e010(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b2e1f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b2e590(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b30c50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b32400(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b325f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b33fc0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b34ac0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b3ad50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b3ae50(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b3ba60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b5ae80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02b5c400(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ba3260(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ba85a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bbb730(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc2280(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc2480(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc2580(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc6170(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc6850(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc72c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bc73d0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bcaa60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bcad00(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bcb1d0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bda710(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bdee60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02bdfa20(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02be19f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02be1af0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c01ea0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c026b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c03680(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c03ca0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c04cb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c0a100(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c12de0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c17760(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c19a90(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c2b3f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c54a90(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02c69d80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02cf9ac0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d064b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d1ad80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d27cd0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d2a2f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d2dcb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d413b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d43fb0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d44190(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d44d00(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d48ea0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d49460(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d4acf0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d4c0b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d56420(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d688b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d689b0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d690f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d691f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d6abe0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d6ace0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d6ade0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02d94450(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dab5f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dae610(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02daf010(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02db1cc0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02db5140(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02db8230(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dba990(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dc2010(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dce2f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd07f0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd2070(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd2170(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd2270(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd2370(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd2590(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd6e20(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd8270(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02dd9a30(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ddef60(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ddf4c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ddf5c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ddf990(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02de1510(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02de54c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02de55c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02e60d80(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02e73730(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02e73830(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02e73ca0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ec98d0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ecc6a0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ed5560(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02edc7e0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02ee6f00(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02f6e1c0(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}

extern "C" void bb_engine_array64_reserve_02f90730(Array64 *array, uint64_t capacity)
{
    engine::array_reserve(array, capacity);
}
