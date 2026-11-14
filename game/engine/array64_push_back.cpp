// SPDX-License-Identifier: GPL-2.0-or-later
// The engine array's push_back for 8-byte elements (engine/array_push_back.h), as the game compiles
// it for each array: 178 copies, the same code as loading/list_push_find.cpp's two, each calling its
// array's own reallocation (to the capacity array_grown_capacity gives). Which arrays they serve is
// not traced here; their elements are taken as 64-bit words.
#include <cstdint>

#include "array_push_back.h"
#include "container.h"
#include "runtime/original.h"

namespace {

using Array64 = engine::Array<uint64_t>;

// Each array's reallocation to a new capacity (not replaced).
RT_ORIGINAL(0x016a5130, reallocate_016a5130, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x016b1290, reallocate_016b1290, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x016d1fc0, reallocate_016d1fc0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x016d9f70, reallocate_016d9f70, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x016e1490, reallocate_016e1490, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0179c3b0, reallocate_0179c3b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0179fd90, reallocate_0179fd90, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x018615f0, reallocate_018615f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0187df30, reallocate_0187df30, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0189a350, reallocate_0189a350, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0189e790, reallocate_0189e790, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x018bf2c0, reallocate_018bf2c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01904bb0, reallocate_01904bb0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0193ed60, reallocate_0193ed60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01b3bf60, reallocate_01b3bf60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01b3c060, reallocate_01b3c060, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01c42780, reallocate_01c42780, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01c62b70, reallocate_01c62b70, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01ceb6a0, reallocate_01ceb6a0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x01e8d280, reallocate_01e8d280, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0215f2f0, reallocate_0215f2f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0223b500, reallocate_0223b500, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02255b50, reallocate_02255b50, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02266eb0, reallocate_02266eb0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02266fb0, reallocate_02266fb0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02273af0, reallocate_02273af0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x022b03e0, reallocate_022b03e0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x022b0720, reallocate_022b0720, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02463540, reallocate_02463540, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02471310, reallocate_02471310, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02471410, reallocate_02471410, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x024fa650, reallocate_024fa650, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x024fa750, reallocate_024fa750, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0254d870, reallocate_0254d870, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02559480, reallocate_02559480, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x025c4630, reallocate_025c4630, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0260fdf0, reallocate_0260fdf0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0265c6a0, reallocate_0265c6a0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x0265e210, reallocate_0265e210, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02682520, reallocate_02682520, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02686cd0, reallocate_02686cd0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02686dd0, reallocate_02686dd0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x026c07c0, reallocate_026c07c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x026c08c0, reallocate_026c08c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x026f4c50, reallocate_026f4c50, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02706ac0, reallocate_02706ac0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02707280, reallocate_02707280, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x027077a0, reallocate_027077a0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a46410, reallocate_02a46410, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a6e960, reallocate_02a6e960, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a6f3d0, reallocate_02a6f3d0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a76a30, reallocate_02a76a30, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a78620, reallocate_02a78620, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a8e840, reallocate_02a8e840, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02a930b0, reallocate_02a930b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02aa3430, reallocate_02aa3430, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b15c20, reallocate_02b15c20, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b17760, reallocate_02b17760, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b18a60, reallocate_02b18a60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b1c130, reallocate_02b1c130, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b220f0, reallocate_02b220f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b26b10, reallocate_02b26b10, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b2a280, reallocate_02b2a280, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b2e010, reallocate_02b2e010, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b2e1f0, reallocate_02b2e1f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b2e590, reallocate_02b2e590, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b30c50, reallocate_02b30c50, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b32400, reallocate_02b32400, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b325f0, reallocate_02b325f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b33fc0, reallocate_02b33fc0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b34ac0, reallocate_02b34ac0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b3ad50, reallocate_02b3ad50, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b3ae50, reallocate_02b3ae50, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b3ba60, reallocate_02b3ba60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b5ae80, reallocate_02b5ae80, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b5c400, reallocate_02b5c400, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b9f870, reallocate_02b9f870, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02b9f980, reallocate_02b9f980, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ba3260, reallocate_02ba3260, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ba85a0, reallocate_02ba85a0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bbb730, reallocate_02bbb730, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc2280, reallocate_02bc2280, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc2480, reallocate_02bc2480, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc2580, reallocate_02bc2580, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc6170, reallocate_02bc6170, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc6850, reallocate_02bc6850, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc72c0, reallocate_02bc72c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bc73d0, reallocate_02bc73d0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bcaa60, reallocate_02bcaa60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bcad00, reallocate_02bcad00, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bcb1d0, reallocate_02bcb1d0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bda710, reallocate_02bda710, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bdee60, reallocate_02bdee60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02bdfa20, reallocate_02bdfa20, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02be19f0, reallocate_02be19f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02be1af0, reallocate_02be1af0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c01ea0, reallocate_02c01ea0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c026b0, reallocate_02c026b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c03680, reallocate_02c03680, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c03ca0, reallocate_02c03ca0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c04cb0, reallocate_02c04cb0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c0a100, reallocate_02c0a100, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c12de0, reallocate_02c12de0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c17760, reallocate_02c17760, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c19a90, reallocate_02c19a90, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c2b3f0, reallocate_02c2b3f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c54a90, reallocate_02c54a90, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02c69d80, reallocate_02c69d80, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02cf9ac0, reallocate_02cf9ac0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d064b0, reallocate_02d064b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d1ad80, reallocate_02d1ad80, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d27cd0, reallocate_02d27cd0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d2a2f0, reallocate_02d2a2f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d2dcb0, reallocate_02d2dcb0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d413b0, reallocate_02d413b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d43fb0, reallocate_02d43fb0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d44190, reallocate_02d44190, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d44d00, reallocate_02d44d00, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d48ea0, reallocate_02d48ea0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d49460, reallocate_02d49460, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d4acf0, reallocate_02d4acf0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d4c0b0, reallocate_02d4c0b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d56420, reallocate_02d56420, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d688b0, reallocate_02d688b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d689b0, reallocate_02d689b0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d690f0, reallocate_02d690f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d691f0, reallocate_02d691f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d6abe0, reallocate_02d6abe0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d6ace0, reallocate_02d6ace0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d6ade0, reallocate_02d6ade0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02d94450, reallocate_02d94450, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dab5f0, reallocate_02dab5f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dae610, reallocate_02dae610, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02daf010, reallocate_02daf010, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02db1cc0, reallocate_02db1cc0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02db5140, reallocate_02db5140, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02db8230, reallocate_02db8230, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dba990, reallocate_02dba990, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dc2010, reallocate_02dc2010, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dce2f0, reallocate_02dce2f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd07f0, reallocate_02dd07f0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd2070, reallocate_02dd2070, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd2170, reallocate_02dd2170, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd2270, reallocate_02dd2270, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd2370, reallocate_02dd2370, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd2590, reallocate_02dd2590, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd6e20, reallocate_02dd6e20, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd8270, reallocate_02dd8270, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd9930, reallocate_02dd9930, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dd9a30, reallocate_02dd9a30, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ddbc40, reallocate_02ddbc40, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02dddf20, reallocate_02dddf20, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ddef60, reallocate_02ddef60, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ddf4c0, reallocate_02ddf4c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ddf5c0, reallocate_02ddf5c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ddf990, reallocate_02ddf990, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02de1510, reallocate_02de1510, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02de54c0, reallocate_02de54c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02de55c0, reallocate_02de55c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02e60d80, reallocate_02e60d80, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02e73730, reallocate_02e73730, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02e73830, reallocate_02e73830, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02e73ca0, reallocate_02e73ca0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ec98d0, reallocate_02ec98d0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ecc6a0, reallocate_02ecc6a0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ed5560, reallocate_02ed5560, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02edc7e0, reallocate_02edc7e0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02ee6f00, reallocate_02ee6f00, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02f6e1c0, reallocate_02f6e1c0, void(Array64 *, uint64_t capacity));
RT_ORIGINAL(0x02f90730, reallocate_02f90730, void(Array64 *, uint64_t capacity));

}  // namespace

extern "C" void bb_engine_array64_push_back_016a5050(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_016a5130);
}

extern "C" void bb_engine_array64_push_back_016b0c80(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_016b1290);
}

extern "C" void bb_engine_array64_push_back_016d1ee0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_016d1fc0);
}

extern "C" void bb_engine_array64_push_back_016d9e90(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_016d9f70);
}

extern "C" void bb_engine_array64_push_back_016e0880(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_016e1490);
}

extern "C" void bb_engine_array64_push_back_0179c2d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0179c3b0);
}

extern "C" void bb_engine_array64_push_back_0179fcb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0179fd90);
}

extern "C" void bb_engine_array64_push_back_01860dc0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_018615f0);
}

extern "C" void bb_engine_array64_push_back_0187e030(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0187df30);
}

extern "C" void bb_engine_array64_push_back_0189a450(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0189a350);
}

extern "C" void bb_engine_array64_push_back_0189a530(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0189a350);
}

extern "C" void bb_engine_array64_push_back_0189e890(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0189e790);
}

extern "C" void bb_engine_array64_push_back_018bf3c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_018bf2c0);
}

extern "C" void bb_engine_array64_push_back_019048a0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01904bb0);
}

extern "C" void bb_engine_array64_push_back_0193ec80(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0193ed60);
}

extern "C" void bb_engine_array64_push_back_01b3bbb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01b3bf60);
}

extern "C" void bb_engine_array64_push_back_01b3bc90(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01b3c060);
}

extern "C" void bb_engine_array64_push_back_01c42880(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01c42780);
}

extern "C" void bb_engine_array64_push_back_01c62780(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01c62b70);
}

extern "C" void bb_engine_array64_push_back_01ceb3b0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01ceb6a0);
}

extern "C" void bb_engine_array64_push_back_01e8d1a0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_01e8d280);
}

extern "C" void bb_engine_array64_push_back_0215ea10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0215f2f0);
}

extern "C" void bb_engine_array64_push_back_0223b420(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0223b500);
}

extern "C" void bb_engine_array64_push_back_02255a70(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02255b50);
}

extern "C" void bb_engine_array64_push_back_02266a70(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02266eb0);
}

extern "C" void bb_engine_array64_push_back_02266b50(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02266fb0);
}

extern "C" void bb_engine_array64_push_back_02273a10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02273af0);
}

extern "C" void bb_engine_array64_push_back_022ab310(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_022b03e0);
}

extern "C" void bb_engine_array64_push_back_022ac430(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_022b0720);
}

extern "C" void bb_engine_array64_push_back_02463460(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02463540);
}

extern "C" void bb_engine_array64_push_back_024708f0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02471310);
}

extern "C" void bb_engine_array64_push_back_024709d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02471410);
}

extern "C" void bb_engine_array64_push_back_024fa490(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_024fa650);
}

extern "C" void bb_engine_array64_push_back_024fa570(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_024fa750);
}

extern "C" void bb_engine_array64_push_back_0254d640(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0254d870);
}

extern "C" void bb_engine_array64_push_back_025593a0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02559480);
}

extern "C" void bb_engine_array64_push_back_025c4550(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_025c4630);
}

extern "C" void bb_engine_array64_push_back_0260f940(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0260fdf0);
}

extern "C" void bb_engine_array64_push_back_0265c0f0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0265c6a0);
}

extern "C" void bb_engine_array64_push_back_0265c860(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_0265e210);
}

extern "C" void bb_engine_array64_push_back_02682310(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02682520);
}

extern "C" void bb_engine_array64_push_back_02686b10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02686cd0);
}

extern "C" void bb_engine_array64_push_back_02686bf0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02686dd0);
}

extern "C" void bb_engine_array64_push_back_026bc560(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_026c07c0);
}

extern "C" void bb_engine_array64_push_back_026bc640(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_026c08c0);
}

extern "C" void bb_engine_array64_push_back_026f47c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_026f4c50);
}

extern "C" void bb_engine_array64_push_back_02705560(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02706ac0);
}

extern "C" void bb_engine_array64_push_back_02705dd0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02707280);
}

extern "C" void bb_engine_array64_push_back_027071a0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_027077a0);
}

extern "C" void bb_engine_array64_push_back_02a451a0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a46410);
}

extern "C" void bb_engine_array64_push_back_02a6c360(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a6f3d0);
}

extern "C" void bb_engine_array64_push_back_02a6e880(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a6e960);
}

extern "C" void bb_engine_array64_push_back_02a76110(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a76a30);
}

extern "C" void bb_engine_array64_push_back_02a77a80(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a78620);
}

extern "C" void bb_engine_array64_push_back_02a8c9c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a8e840);
}

extern "C" void bb_engine_array64_push_back_02a92fd0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02a930b0);
}

extern "C" void bb_engine_array64_push_back_02aa2e10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02aa3430);
}

extern "C" void bb_engine_array64_push_back_02b15b40(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b15c20);
}

extern "C" void bb_engine_array64_push_back_02b17680(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b17760);
}

extern "C" void bb_engine_array64_push_back_02b18970(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b18a60);
}

extern "C" void bb_engine_array64_push_back_02b1c050(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b1c130);
}

extern "C" void bb_engine_array64_push_back_02b22010(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b220f0);
}

extern "C" void bb_engine_array64_push_back_02b267d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b26b10);
}

extern "C" void bb_engine_array64_push_back_02b29eb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b2a280);
}

extern "C" void bb_engine_array64_push_back_02b2dce0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b2e010);
}

extern "C" void bb_engine_array64_push_back_02b2e110(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b2e1f0);
}

extern "C" void bb_engine_array64_push_back_02b2e390(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b2e590);
}

extern "C" void bb_engine_array64_push_back_02b30330(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b30c50);
}

extern "C" void bb_engine_array64_push_back_02b32310(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b32400);
}

extern "C" void bb_engine_array64_push_back_02b32500(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b325f0);
}

extern "C" void bb_engine_array64_push_back_02b33ed0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b33fc0);
}

extern "C" void bb_engine_array64_push_back_02b34780(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b34ac0);
}

extern "C" void bb_engine_array64_push_back_02b38c10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b3ae50);
}

extern "C" void bb_engine_array64_push_back_02b3a3c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b3ad50);
}

extern "C" void bb_engine_array64_push_back_02b3b980(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b3ba60);
}

extern "C" void bb_engine_array64_push_back_02b55040(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b5c400);
}

extern "C" void bb_engine_array64_push_back_02b56440(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b5ae80);
}

extern "C" void bb_engine_array64_push_back_02b9e290(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b9f980);
}

extern "C" void bb_engine_array64_push_back_02b9e370(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02b9f870);
}

extern "C" void bb_engine_array64_push_back_02ba2c00(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ba3260);
}

extern "C" void bb_engine_array64_push_back_02ba8070(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ba85a0);
}

extern "C" void bb_engine_array64_push_back_02bba920(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bbb730);
}

extern "C" void bb_engine_array64_push_back_02bc0ed0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc2580);
}

extern "C" void bb_engine_array64_push_back_02bc0fb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc2480);
}

extern "C" void bb_engine_array64_push_back_02bc1940(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc2280);
}

extern "C" void bb_engine_array64_push_back_02bc6090(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc6170);
}

extern "C" void bb_engine_array64_push_back_02bc6770(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc6850);
}

extern "C" void bb_engine_array64_push_back_02bc6e90(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc73d0);
}

extern "C" void bb_engine_array64_push_back_02bc6f70(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bc72c0);
}

extern "C" void bb_engine_array64_push_back_02bca980(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bcaa60);
}

extern "C" void bb_engine_array64_push_back_02bcac20(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bcad00);
}

extern "C" void bb_engine_array64_push_back_02bcb0f0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bcb1d0);
}

extern "C" void bb_engine_array64_push_back_02bda440(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bda710);
}

extern "C" void bb_engine_array64_push_back_02bdf5f0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bdfa20);
}

extern "C" void bb_engine_array64_push_back_02be1770(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02be1af0);
}

extern "C" void bb_engine_array64_push_back_02be1850(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02be19f0);
}

extern "C" void bb_engine_array64_push_back_02bf2ee0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c04cb0);
}

extern "C" void bb_engine_array64_push_back_02bf2fc0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c03ca0);
}

extern "C" void bb_engine_array64_push_back_02bfd230(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c03680);
}

extern "C" void bb_engine_array64_push_back_02c015d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c01ea0);
}

extern "C" void bb_engine_array64_push_back_02c025d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c026b0);
}

extern "C" void bb_engine_array64_push_back_02c03bb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c03ca0);
}

extern "C" void bb_engine_array64_push_back_02c046d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bdfa20);
}

extern "C" void bb_engine_array64_push_back_02c049c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02bdee60);
}

extern "C" void bb_engine_array64_push_back_02c08dc0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c0a100);
}

extern "C" void bb_engine_array64_push_back_02c12250(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c12de0);
}

extern "C" void bb_engine_array64_push_back_02c16840(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c17760);
}

extern "C" void bb_engine_array64_push_back_02c19850(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c19a90);
}

extern "C" void bb_engine_array64_push_back_02c1a250(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c17760);
}

extern "C" void bb_engine_array64_push_back_02c2b300(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c2b3f0);
}

extern "C" void bb_engine_array64_push_back_02c52f30(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c54a90);
}

extern "C" void bb_engine_array64_push_back_02c69460(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02c69d80);
}

extern "C" void bb_engine_array64_push_back_02cf9870(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02cf9ac0);
}

extern "C" void bb_engine_array64_push_back_02d063d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d064b0);
}

extern "C" void bb_engine_array64_push_back_02d18230(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d27cd0);
}

extern "C" void bb_engine_array64_push_back_02d18310(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d1ad80);
}

extern "C" void bb_engine_array64_push_back_02d2a210(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d2a2f0);
}

extern "C" void bb_engine_array64_push_back_02d2ccf0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d2dcb0);
}

extern "C" void bb_engine_array64_push_back_02d3ff30(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d413b0);
}

extern "C" void bb_engine_array64_push_back_02d416c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d27cd0);
}

extern "C" void bb_engine_array64_push_back_02d43ed0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d43fb0);
}

extern "C" void bb_engine_array64_push_back_02d440b0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d44190);
}

extern "C" void bb_engine_array64_push_back_02d44c20(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d44d00);
}

extern "C" void bb_engine_array64_push_back_02d48c40(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d48ea0);
}

extern "C" void bb_engine_array64_push_back_02d49370(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d49460);
}

extern "C" void bb_engine_array64_push_back_02d4ac10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d4acf0);
}

extern "C" void bb_engine_array64_push_back_02d4b000(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d4c0b0);
}

extern "C" void bb_engine_array64_push_back_02d55f20(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d56420);
}

extern "C" void bb_engine_array64_push_back_02d68440(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d691f0);
}

extern "C" void bb_engine_array64_push_back_02d68520(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d691f0);
}

extern "C" void bb_engine_array64_push_back_02d68600(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d690f0);
}

extern "C" void bb_engine_array64_push_back_02d686f0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d689b0);
}

extern "C" void bb_engine_array64_push_back_02d687d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d688b0);
}

extern "C" void bb_engine_array64_push_back_02d6a870(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d6ade0);
}

extern "C" void bb_engine_array64_push_back_02d6a950(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d6ace0);
}

extern "C" void bb_engine_array64_push_back_02d6aa30(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d6abe0);
}

extern "C" void bb_engine_array64_push_back_02d92bb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02d94450);
}

extern "C" void bb_engine_array64_push_back_02dab230(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dab5f0);
}

extern "C" void bb_engine_array64_push_back_02dae530(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dae610);
}

extern "C" void bb_engine_array64_push_back_02daee50(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02daf010);
}

extern "C" void bb_engine_array64_push_back_02db1be0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02db1cc0);
}

extern "C" void bb_engine_array64_push_back_02db5020(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02db5140);
}

extern "C" void bb_engine_array64_push_back_02db7c60(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02db8230);
}

extern "C" void bb_engine_array64_push_back_02dba280(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dba990);
}

extern "C" void bb_engine_array64_push_back_02dc1f10(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dc2010);
}

extern "C" void bb_engine_array64_push_back_02dce210(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dce2f0);
}

extern "C" void bb_engine_array64_push_back_02dcf190(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd07f0);
}

extern "C" void bb_engine_array64_push_back_02dd1cf0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd2370);
}

extern "C" void bb_engine_array64_push_back_02dd1dd0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd2270);
}

extern "C" void bb_engine_array64_push_back_02dd1eb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd2170);
}

extern "C" void bb_engine_array64_push_back_02dd1f90(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd2070);
}

extern "C" void bb_engine_array64_push_back_02dd2470(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd2590);
}

extern "C" void bb_engine_array64_push_back_02dd6d30(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd6e20);
}

extern "C" void bb_engine_array64_push_back_02dd7cb0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd8270);
}

extern "C" void bb_engine_array64_push_back_02dd9440(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd9a30);
}

extern "C" void bb_engine_array64_push_back_02dd9520(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dd9930);
}

extern "C" void bb_engine_array64_push_back_02ddbb60(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ddbc40);
}

extern "C" void bb_engine_array64_push_back_02ddd0f0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ddf5c0);
}

extern "C" void bb_engine_array64_push_back_02ddd1d0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ddf4c0);
}

extern "C" void bb_engine_array64_push_back_02ddd310(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ddf4c0);
}

extern "C" void bb_engine_array64_push_back_02ddd630(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ddef60);
}

extern "C" void bb_engine_array64_push_back_02ddd710(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02dddf20);
}

extern "C" void bb_engine_array64_push_back_02ddf860(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ddf990);
}

extern "C" void bb_engine_array64_push_back_02de1420(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02de1510);
}

extern "C" void bb_engine_array64_push_back_02de46a0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02de55c0);
}

extern "C" void bb_engine_array64_push_back_02de4780(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02de54c0);
}

extern "C" void bb_engine_array64_push_back_02e60b70(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02e60d80);
}

extern "C" void bb_engine_array64_push_back_02e70980(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02e73830);
}

extern "C" void bb_engine_array64_push_back_02e70f90(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02e73730);
}

extern "C" void bb_engine_array64_push_back_02e73930(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02e73ca0);
}

extern "C" void bb_engine_array64_push_back_02ec88e0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ec98d0);
}

extern "C" void bb_engine_array64_push_back_02ec89c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ec98d0);
}

extern "C" void bb_engine_array64_push_back_02ecc5c0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ecc6a0);
}

extern "C" void bb_engine_array64_push_back_02ed5480(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ed5560);
}

extern "C" void bb_engine_array64_push_back_02edc700(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02edc7e0);
}

extern "C" void bb_engine_array64_push_back_02ee6e20(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02ee6f00);
}

extern "C" void bb_engine_array64_push_back_02f6e0e0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02f6e1c0);
}

extern "C" void bb_engine_array64_push_back_02f8eda0(Array64 *array, const uint64_t *item)
{
    engine::array_push_back(array, item, reallocate_02f90730);
}
