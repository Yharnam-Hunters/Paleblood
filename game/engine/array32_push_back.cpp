// SPDX-License-Identifier: GPL-2.0-or-later
// The engine array's push_back for 4-byte elements (engine/array_push_back.h), as the game compiles
// it for each array: 14 copies, the same code as param/id_array_push_back.cpp's three, each calling its
// array's own reallocation (to the capacity array_grown_capacity gives). Which arrays they serve is
// not traced here; their elements are taken as 32-bit ids.
#include <cstdint>

#include "array_push_back.h"
#include "container.h"
#include "runtime/original.h"

namespace {

using Array32 = engine::Array<uint32_t>;

// Each array's reallocation to a new capacity (not replaced).
RT_ORIGINAL(0x016e8680, reallocate_016e8680, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x01c151f0, reallocate_01c151f0, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x01effa90, reallocate_01effa90, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x022ae660, reallocate_022ae660, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02b9fa90, reallocate_02b9fa90, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02bda340, reallocate_02bda340, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02bda600, reallocate_02bda600, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02bdef60, reallocate_02bdef60, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02c045d0, reallocate_02c045d0, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02c26a50, reallocate_02c26a50, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02c2b200, reallocate_02c2b200, void(Array32 *, uint64_t capacity));
RT_ORIGINAL(0x02d75ce0, reallocate_02d75ce0, void(Array32 *, uint64_t capacity));

}  // namespace

extern "C" void bb_engine_array32_push_back_016e85a0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_016e8680);
}

extern "C" void bb_engine_array32_push_back_01b0d510(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02d75ce0);
}

extern "C" void bb_engine_array32_push_back_01c15110(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_01c151f0);
}

extern "C" void bb_engine_array32_push_back_01efddb0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_01effa90);
}

extern "C" void bb_engine_array32_push_back_022a89a0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_022ae660);
}

extern "C" void bb_engine_array32_push_back_02b9e1b0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02b9fa90);
}

extern "C" void bb_engine_array32_push_back_02bda260(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02bda340);
}

extern "C" void bb_engine_array32_push_back_02bda520(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02bda600);
}

extern "C" void bb_engine_array32_push_back_02bdc9b0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02bda600);
}

extern "C" void bb_engine_array32_push_back_02c044f0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02c045d0);
}

extern "C" void bb_engine_array32_push_back_02c048e0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02bdef60);
}

extern "C" void bb_engine_array32_push_back_02c249a0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02c26a50);
}

extern "C" void bb_engine_array32_push_back_02c2abc0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02c2b200);
}

extern "C" void bb_engine_array32_push_back_02d75be0(Array32 *array, const uint32_t *item)
{
    engine::array_push_back(array, item, reallocate_02d75ce0);
}
