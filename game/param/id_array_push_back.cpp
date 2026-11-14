// SPDX-License-Identifier: GPL-2.0-or-later
// Param: the push_back of the engine's 32-bit id arrays, which the table walks (row_iteration.cpp),
// the map lookups (map_lookups.cpp) and the physics instance construction call as originals. The
// game has four copies of the same code (engine/array_push_back.h), two on each of two copies of
// the id array's reallocation:
// - param_id_array_push_back_02bc1bb0 and param_protector_id_array_push_back_019f8d70 grow through
//   0x02bc1ca0 (physics_ids_reserve_02bc1ca0);
// - param_id_array_push_back_copy_016da6a0 and param_weapon_id_array_push_back_01b0d5f0 grow
//   through 0x016da780, the same reallocation compiled again.
#include <cstdint>

#include "../engine/array_push_back.h"
#include "runtime/original.h"

namespace {

using IdArray = engine::Array<int32_t>;

RT_ORIGINAL(0x02bc1ca0, id_array_reserve, void(IdArray *, uint64_t capacity));
RT_ORIGINAL(0x016da780, id_array_reserve_copy, void(IdArray *, uint64_t capacity));

}  // namespace

extern "C" void bb_param_id_array_push_back_02bc1bb0(IdArray *array, const int32_t *id)
{
    engine::array_push_back(array, id, id_array_reserve);
}

extern "C" void bb_param_protector_id_array_push_back_019f8d70(IdArray *array, const int32_t *id)
{
    engine::array_push_back(array, id, id_array_reserve);
}

extern "C" void bb_param_id_array_push_back_copy_016da6a0(IdArray *array, const int32_t *id)
{
    engine::array_push_back(array, id, id_array_reserve_copy);
}

extern "C" void bb_param_weapon_id_array_push_back_01b0d5f0(IdArray *array, const int32_t *id)
{
    engine::array_push_back(array, id, id_array_reserve_copy);
}
