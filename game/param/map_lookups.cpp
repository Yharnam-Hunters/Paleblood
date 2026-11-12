// SPDX-License-Identifier: GPL-2.0-or-later
// Lookups in the two map tables of the solo parameter repository (param_repository.h):
// GameMapParam, a map's lighting set-up, found by the map's number; and ReturnPointParam, the
// return points (lamps) players warp to and revive at, which the game searches row by row by
// entity id, warp chair number, or map.
#include <cstring>

#include "../engine/container.h"
#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::RowLookup;
using param::TableHeader;

// A map's id as the game passes it around: the four numbers of its name mAA_BB_CC_DD, one per
// byte, area highest. All ones means no map.
struct PackedMapId {
    uint8_t part_dd;
    uint8_t part_cc;
    uint8_t block;
    uint8_t area;
};
static_assert(sizeof(PackedMapId) == 4 && offsetof(PackedMapId, area) == 3);
constexpr uint32_t no_map = 0xffffffff;

// A map's row id in GameMapParam: its name's numbers as one decimal number, AABBCCDD.
constexpr int32_t area_scale = 1000000, block_scale = 10000, part_cc_scale = 100;
// The row used for maps without their own row.
constexpr uint32_t default_game_map_id = 0;

// A ReturnPointParam row (the field names are the community's, in comments).
struct ReturnPointRow {
    int16_t area;                  // areaNo
    int16_t block;                 // blockNo
    int32_t entity_id;             // returnPointEntityId: the return point's object in the map
    int32_t return_animation_id;   // returnAnimId
    uint8_t registers_as_revival;  // isRegistDeadReturn
    int8_t warp_chair_number;      // warpChairNo: the point's slot in the warp menu; -1: none
};
static_assert(offsetof(ReturnPointRow, entity_id) == 0x4);
static_assert(offsetof(ReturnPointRow, warp_chair_number) == 0xd);
constexpr int8_t no_warp_chair = -1;
constexpr int32_t not_found = -1;

// Rows are read with memcpy: the tables give no alignment guarantee.
ReturnPointRow read_return_point(const uint8_t *data)
{
    ReturnPointRow row;
    std::memcpy(&row, data, sizeof(row));
    return row;
}

RT_ORIGINAL(0x02bc1bb0, entity_ids_push_back, void(engine::Array<int32_t> *, const int32_t *id));

// Every ReturnPointParam row, in file order, as `visit(row data)` until it returns true.
// A row number past the count (the count may change while it runs) or a row at address zero
// is skipped. Without a table nothing is visited.
template <typename Visit> void scan_return_points(const Visit &visit)
{
    const TableHeader *table = loaded_table(ParamType::return_point);
    if (table == nullptr) return;
    const uint32_t count = table->row_count;
    for (uint32_t row_number = 0; row_number < count; ++row_number) {
        const uint8_t *data = param::row_data_if_present(table, row_number);
        if (data != nullptr && visit(data)) return;
    }
}

}  // namespace

// The GameMapParam row of a map, or the default row (id 0) when the map has none; the result's
// id is the map's row id, or -1 for no map.
extern "C" void bb_param_find_row_game_map(RowLookup *out, const PackedMapId *map)
{
    uint32_t packed;
    std::memcpy(&packed, map, sizeof(packed));
    int32_t id = -1;
    const uint8_t *row = nullptr;
    if (packed != no_map) {
        id = map->area * area_scale + map->block * block_scale + map->part_cc * part_cc_scale + map->part_dd;
        if (id >= 0) {
            if (const TableHeader *table = loaded_table(ParamType::game_map)) {
                row = param::find_row_in_table(table, static_cast<uint32_t>(id));
                if (row == nullptr) row = param::find_row_in_table(table, default_game_map_id);
            }
        }
    }
    out->id = id;
    out->row = row;
}

// The warp chair number of the first return point with this entity id, or -1 (also for a
// negative id, which is not searched).
extern "C" int32_t bb_param_return_point_warp_chair(int32_t entity_id)
{
    int32_t chair = not_found;
    if (entity_id < 0) return chair;
    scan_return_points([&](const uint8_t *data) {
        const ReturnPointRow row = read_return_point(data);
        if (row.entity_id != entity_id) return false;
        chair = row.warp_chair_number;
        return true;
    });
    return chair;
}

// The entity id of the first return point with this warp chair number, or -1 (also for a
// negative number, which is not searched).
extern "C" int32_t bb_param_return_point_entity(int32_t warp_chair)
{
    int32_t entity_id = not_found;
    if (warp_chair < 0) return entity_id;
    scan_return_points([&](const uint8_t *data) {
        const ReturnPointRow row = read_return_point(data);
        if (row.warp_chair_number != warp_chair) return false;
        entity_id = row.entity_id;
        return true;
    });
    return entity_id;
}

// Appends to `ids` the entity id of every return point in the map area/block that has a warp
// chair, in file order. The id is pushed by address, straight from the row.
extern "C" void bb_param_return_points_of_map(engine::Array<int32_t> *ids, int32_t area, int32_t block)
{
    scan_return_points([&](const uint8_t *data) {
        const ReturnPointRow row = read_return_point(data);
        if (row.area == area && row.block == block && row.warp_chair_number != no_warp_chair)
            entity_ids_push_back(ids, reinterpret_cast<const int32_t *>(data + offsetof(ReturnPointRow, entity_id)));
        return false;
    });
}
