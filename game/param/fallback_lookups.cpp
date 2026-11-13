// SPDX-License-Identifier: GPL-2.0-or-later
// Row lookups of the solo parameter repository (param_repository.h) that go beyond the plain
// "row with this id": a GameAreaParam lookup that sends a range of ids to one row, an
// ItemLotParam_Lvdep (level-dependent item lot) lookup that picks a row by level, the lock-on
// camera's distance with its fallbacks, the menu layout lookup, and a check that is all the
// compiler left of one accessor.
#include <cstring>

#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::RowLookup;
using param::TableHeader;

constexpr int32_t no_row = -1;

// GameAreaParam ids from 2900100 to 2900799 all read the row with id 2900100.
constexpr uint32_t shared_game_area_id = 2900100;
constexpr uint32_t shared_game_area_count = 700;

// ItemLotParam_Lvdep ids come in groups of 100: a group's first id plus a level.
constexpr int32_t lot_group_size = 100;

// A LockCamParam row: only the field the camera reads (the community's name for it is
// chrLockRangeMaxRadius, the largest lock-on distance).
constexpr size_t lock_range_offset = 0x10;
// The object that asks keeps its own distance for when the table has no row for it.
constexpr size_t default_lock_range_offset = 0x2924;

float read_float(const uint8_t *at)
{
    float value;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

}  // namespace

// The GameAreaParam row of an id (as the plain lookups: none for a negative id, which leaves the
// repository unread), except that the ids of the shared range all get the range's first row.
extern "C" void bb_param_find_row_game_area(RowLookup *out, int32_t id)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(ParamType::game_area)) {
            const uint32_t from_shared = static_cast<uint32_t>(id) - shared_game_area_id;
            const uint32_t searched = from_shared < shared_game_area_count ? shared_game_area_id : static_cast<uint32_t>(id);
            row = param::find_row_in_table(table, searched);
        }
    }
    out->id = id;
    out->row = row;
}

// The ItemLotParam_Lvdep row for an id's group at a level: starting at the group's first id (the
// id rounded down to a multiple of 100), the last of the consecutive rows in file order whose ids
// stay within the first id plus `level`. The result is id -1 and no row when the id is negative,
// the table is not loaded, the group's first id is not in the table (or the index gives it no
// row) or the level is negative. A row the index places past the count is returned with no row.
extern "C" void bb_param_find_row_item_lot_lvdep(RowLookup *out, int32_t id, int32_t level)
{
    out->id = no_row;
    out->row = nullptr;
    if (id < 0) return;
    const TableHeader *table = loaded_table(ParamType::item_lot_lvdep);
    if (table == nullptr) return;
    const int32_t first = id / lot_group_size * lot_group_size;
    const int32_t last = static_cast<int32_t>(static_cast<uint32_t>(first) + static_cast<uint32_t>(level));
    const int32_t first_row = param::find_row_number(table, static_cast<uint32_t>(first));
    if (first_row < 0 || first > last) return;
    uint32_t row_number = static_cast<uint32_t>(first_row);
    int32_t row_id = first;
    for (;;) {
        out->id = row_id;
        out->row = param::row_data_if_present(table, row_number);
        ++row_number;
        // Ids compare as signed numbers here.
        const int32_t next_id =
            row_number < table->row_count ? static_cast<int32_t>(param::describe_row(table, row_number).id) : no_row;
        if (next_id < first || next_id > last) return;
        row_id = next_id;
    }
}

// The largest lock-on distance for a LockCamParam id: the row's, else row 0's, else the asking
// object's own. The id is searched as an unsigned number, and a row whose data is at address
// zero counts as missing.
extern "C" float bb_param_lock_cam_range(const uint8_t *owner, uint32_t id)
{
    const float fallback = read_float(owner + default_lock_range_offset);
    const TableHeader *table = loaded_table(ParamType::lock_cam);
    if (table == nullptr) return fallback;
    const uint8_t *row = param::find_row_in_table(table, id);
    if (row == nullptr) row = param::find_row_in_table(table, 0);
    return row == nullptr ? fallback : read_float(row + lock_range_offset);
}

// The MenuPropertyLayoutParam row of an id (searched unsigned), or null.
extern "C" const uint8_t *bb_param_menu_property_layout_row(uint32_t id)
{
    const TableHeader *table = loaded_table(ParamType::menu_property_layout);
    if (table == nullptr) return nullptr;
    return param::find_row_in_table(table, id);
}

// An accessor reduced to its repository check: for an id of 0 or more, a missing repository is
// reported.
extern "C" void bb_param_check_repository_for_id(int32_t id)
{
    if (id >= 0) engine::require(param::solo_param_repository, param::solo_param_repository_name.address());
}
