// SPDX-License-Identifier: GPL-2.0-or-later
// Row lookups that do more than find one row: the equipment lookups that also find the
// reinforcement row, the menu tables read as runs of consecutive ids, the menu property lookup
// with its built-in fallback, and lookups with a side condition or side effect. They share
// the search of param_repository.h. The menu ones run at the title menus and in character creation.
#include <cstring>

#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::RowLookup;
using param::TableHeader;

// Equipment ids carry the reinforcement level in their last two decimal digits: the base row
// is the id rounded down to a multiple of 100.
constexpr int32_t reinforcement_levels = 100;

// An equipment lookup's result: the row of the id's base item, and the reinforcement row for
// the item's reinforcement type at the id's level.
struct EquipRowLookup {
    RowLookup equip;      // the id asked for, and the base item's row
    int32_t base_id;      // the id rounded down to its base item, or -1
    uint8_t unknown_0x14[0x4];
    RowLookup reinforce;  // the reinforcement type's row for the level, or the -1 lookup
};
static_assert(offsetof(EquipRowLookup, base_id) == 0x10);
static_assert(offsetof(EquipRowLookup, reinforce) == 0x18);

// The reinforcement type of an equipment row (the community's reinforceTypeId), at a different
// place in weapon and armor rows.
constexpr size_t weapon_reinforce_type_offset = 0xd6;
constexpr size_t protector_reinforce_type_offset = 0xc8;

RT_ORIGINAL(0x0232c930, find_reinforce_weapon_row, void(RowLookup *, int32_t));
RT_ORIGINAL(0x02326200, find_reinforce_protector_row, void(RowLookup *, int32_t));

int16_t reinforce_type(const uint8_t *row, size_t offset)
{
    int16_t type;
    std::memcpy(&type, row + offset, sizeof(type));
    return type;
}

template <typename Lookup>
void find_equip_row(EquipRowLookup *out, int32_t id, ParamType type, size_t reinforce_type_offset,
                    const Lookup &find_reinforce_row)
{
    int32_t base_id = -1;
    const uint8_t *row = nullptr;
    if (id >= 0) {
        base_id = id / reinforcement_levels * reinforcement_levels;
        if (const TableHeader *table = loaded_table(type))
            row = param::find_row_in_table(table, static_cast<uint32_t>(base_id));
    }
    out->equip.id = id;
    out->equip.row = row;
    out->base_id = base_id;
    // Without a base row the reinforcement lookup still runs, for id -1 (no row).
    int32_t reinforce_id = -1;
    if (row != nullptr) reinforce_id = id % reinforcement_levels + reinforce_type(row, reinforce_type_offset);
    find_reinforce_row(&out->reinforce, reinforce_id);
}

// A run of rows with consecutive ids: the first row and how many follow it without a gap.
struct RowRun {
    const uint8_t *first;
    int32_t count;
};
static_assert(offsetof(RowRun, count) == 0x8);

// The run starting at `first_id` in the table of `type`; without one, the result is `none` and
// `none_count`. The ids wrap around at 2^32, like the original's 32-bit addition.
void find_row_run(RowRun *out, uint32_t first_id, ParamType type, const uint8_t *none, int32_t none_count)
{
    out->first = none;
    out->count = none_count;
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return;
    const uint8_t *row = param::find_row_in_table(table, first_id);
    if (row == nullptr) return;
    out->first = row;
    // The count is stored before each next id is looked up.
    for (uint32_t count = 1;; ++count) {
        out->count = static_cast<int32_t>(count);
        if (param::find_row_in_table(table, first_id + count) == nullptr) return;
    }
}

// The menu property fallback: built-in rows the lookup uses when the table has no row for an id.
constexpr size_t menu_property_spec_row_size = 0x20;
struct BuiltInMenuPropertySpec {
    uint32_t id;
    uint8_t row[menu_property_spec_row_size];
};
static_assert(sizeof(BuiltInMenuPropertySpec) == 0x24);
constexpr int built_in_menu_property_spec_count = 13;

RT_GLOBAL(0x05528730, built_in_menu_property_specs,
          BuiltInMenuPropertySpec[built_in_menu_property_spec_count]);
// The built-in rows a NewMenuColorTableParam run falls back to when the id has none.
RT_GLOBAL(0x055286e0, default_new_menu_color_rows, const uint8_t);
constexpr int32_t default_new_menu_color_count = 16;
// Where the DungeonFeatureParam lookup publishes its table's header word 0x06 (as 32 bits).
RT_GLOBAL(0x05992cb0, dungeon_feature_header_word, uint32_t);

// The NpcParam lookup also returns two header words, and reports a table older than it supports.
struct NpcRowLookup {
    int32_t id;
    uint16_t header_word_06;
    uint16_t data_version;
    const uint8_t *row;
};
static_assert(offsetof(NpcRowLookup, header_word_06) == 0x4);
static_assert(offsetof(NpcRowLookup, data_version) == 0x6);
static_assert(offsetof(NpcRowLookup, row) == 0x8);
constexpr uint16_t newest_unsupported_npc_version = 2;
// Its report: "The NPC parameter version is old. Please re-pull the data."
RT_GLOBAL(0x04d38a20, npc_param_source_file, const char);
RT_GLOBAL(0x04d38a5a, npc_param_old_version_message, const char);
constexpr int32_t npc_param_old_version_line = 0x4a;

// ItemLotParam rows are only used from data version 2 on.
constexpr uint16_t first_item_lot_version = 2;

}  // namespace

extern "C" void bb_param_find_row_equip_param_weapon(EquipRowLookup *out, int32_t id)
{
    find_equip_row(out, id, ParamType::equip_param_weapon, weapon_reinforce_type_offset,
                   find_reinforce_weapon_row);
}

extern "C" void bb_param_find_row_equip_param_protector(EquipRowLookup *out, int32_t id)
{
    find_equip_row(out, id, ParamType::equip_param_protector, protector_reinforce_type_offset,
                   find_reinforce_protector_row);
}

// The character creation menu's list items, read as a run of consecutive ids.
extern "C" void bb_param_find_rows_char_make_menu_list_item(RowRun *out, int32_t first_id)
{
    find_row_run(out, static_cast<uint32_t>(first_id), ParamType::char_make_menu_list_item, nullptr, 0);
}

extern "C" void bb_param_find_rows_new_menu_color_table(RowRun *out, int32_t first_id)
{
    find_row_run(out, static_cast<uint32_t>(first_id), ParamType::new_menu_color_table,
                 default_new_menu_color_rows.address(), default_new_menu_color_count);
}

// The same run as find_row_run, except that this copy fetches the table again for every id.
extern "C" void bb_param_find_rows_menu_value_table(RowRun *out, int32_t first_id)
{
    out->first = nullptr;
    out->count = 0;
    for (uint32_t id = static_cast<uint32_t>(first_id);; ++id) {
        const TableHeader *table = loaded_table(ParamType::menu_value_table);
        if (table == nullptr) return;
        const uint8_t *row = param::find_row_in_table(table, id);
        if (row == nullptr) return;
        if (id == static_cast<uint32_t>(first_id)) out->first = row;
        ++out->count;
    }
}

// A menu property row: the table's, else the built-in row with that id, else the first
// built-in row. Negative ids are searched like any other (as unsigned).
extern "C" const uint8_t *bb_param_menu_property_spec(int32_t id)
{
    const uint32_t key = static_cast<uint32_t>(id);
    if (const TableHeader *table = loaded_table(ParamType::menu_property_spec)) {
        if (const uint8_t *row = param::find_row_in_table(table, key)) return row;
    }
    BuiltInMenuPropertySpec(&built_in)[built_in_menu_property_spec_count] = built_in_menu_property_specs.get();
    for (const BuiltInMenuPropertySpec &spec : built_in) {
        if (spec.id == key) return spec.row;
    }
    return built_in[0].row;
}

extern "C" void bb_param_find_row_item_lot(RowLookup *out, int32_t id)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        const TableHeader *table = loaded_table(ParamType::item_lot);
        if (table != nullptr && table->data_version >= first_item_lot_version)
            row = param::find_row_in_table(table, static_cast<uint32_t>(id));
    }
    out->id = id;
    out->row = row;
}

// As find_row, and every search of the table, found or not, publishes its header word 0x06.
extern "C" void bb_param_find_row_dungeon_feature(RowLookup *out, int32_t id)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(ParamType::dungeon_feature)) {
            row = param::find_row_in_table(table, static_cast<uint32_t>(id));
            dungeon_feature_header_word.get() = table->header_word_06;
        }
    }
    out->id = id;
    out->row = row;
}

// As find_row, and also returns the table's header word 0x06 and data version (before the
// search). A table of version 2 or older is reported and searched anyway.
extern "C" void bb_param_find_row_npc(NpcRowLookup *out, int32_t id)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(ParamType::npc)) {
            out->header_word_06 = table->header_word_06;
            out->data_version = table->data_version;
            if (table->data_version <= newest_unsupported_npc_version)
                engine::fatal_error(npc_param_source_file.address(), npc_param_old_version_line,
                                    npc_param_old_version_message.address());
            row = param::find_row_in_table(table, static_cast<uint32_t>(id));
        }
    }
    out->id = id;
    out->row = row;
}
