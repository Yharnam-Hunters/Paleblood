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

// The weapon steps: from the EquipParamWeapon row with id `id`, move `step` rows on (or back) in
// file order, look the id found there up as a weapon, and stop at the first whose row has the
// flag `mode` asks for (bit 0, 1 or 2 of the 40-bit flag field at +0x100; a mode other than 0-2
// stops at once). A negative id gives the first (last) row's id. -1 when the table is not loaded
// or empty, an id is not in the table, or the step leaves the table. The looked-up row is read
// without a check: an id whose base item (rounded down to 100) is missing would crash the original.
namespace {

constexpr size_t weapon_step_flags_offset = 0x100;
constexpr int32_t weapon_step_modes = 3;

bool weapon_step_stops(int32_t id, int32_t mode)
{
    if (mode < 0 || mode >= weapon_step_modes) return true;
    EquipRowLookup lookup;
    bb_param_find_row_equip_param_weapon(&lookup, id);
    return lookup.equip.row[weapon_step_flags_offset] & (1u << mode);
}

template <typename Step> int32_t step_weapon_id(int32_t id, int32_t mode, bool backwards, const Step &step)
{
    const TableHeader *table = loaded_table(ParamType::equip_param_weapon);
    if (table == nullptr) return -1;
    if (id < 0) {
        if (table->row_count == 0) return -1;
        return static_cast<int32_t>(param::describe_row(table, backwards ? table->row_count - 1u : 0).id);
    }
    for (;;) {
        const int32_t row_number = param::find_row_number(table, static_cast<uint32_t>(id));
        if (row_number < 0) return -1;
        const uint32_t stepped = step(static_cast<uint32_t>(row_number));
        if (stepped >= table->row_count) return -1;
        id = static_cast<int32_t>(param::describe_row(table, stepped).id);
        if (weapon_step_stops(id, mode)) return id;
    }
}

}  // namespace

extern "C" int32_t bb_param_next_weapon_id_with_flag(int32_t id, int32_t step, int32_t mode)
{
    return step_weapon_id(id, mode, false, [step](uint32_t row_number) { return row_number + static_cast<uint32_t>(step); });
}

extern "C" int32_t bb_param_previous_weapon_id_with_flag(int32_t id, int32_t step, int32_t mode)
{
    return step_weapon_id(id, mode, true, [step](uint32_t row_number) { return row_number - static_cast<uint32_t>(step); });
}

// The set checks: each keeps a cache, built on its first call, of up to N rows of one table that
// belong to a set (the row's 16-bit set number at a fixed offset is not negative): their ids and
// set numbers, -1 in the unused entries. Given a looked-up row and a bit set over the cache
// entries, a check marks the entry with the row's id and returns the row's set number when every
// other entry of that set is marked already, else -1 (also for a missing row or a negative id or
// set number). A table that is not loaded on the first call leaves the cache empty for good.
namespace {

RT_ORIGINAL(0x02fbe688, c_memset, void *(void *, int32_t, uint64_t));
constexpr int32_t unused_entry_byte = 0xff;

struct SetCache {
    bool *built;
    int32_t *ids;
    int32_t *sets;
    int32_t capacity;
};

// Fills the cache from the rows with a set number that `listed` accepts (given the row's id), in
// file order, until it is full.
template <typename Listed>
void build_set_cache(const SetCache &cache, ParamType type, size_t set_offset, const Listed &listed)
{
    // The C library's memset with 0xff bytes: every entry -1.
    c_memset(cache.ids, unused_entry_byte, sizeof(int32_t) * cache.capacity);
    c_memset(cache.sets, unused_entry_byte, sizeof(int32_t) * cache.capacity);
    *cache.built = true;
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return;
    const uint32_t count = table->row_count;
    int32_t entries = 0;
    for (uint32_t row_number = 0; row_number < count; ++row_number) {
        const uint8_t *data = param::row_data_if_present(table, row_number);
        if (data == nullptr) continue;
        int16_t set;
        std::memcpy(&set, data + set_offset, sizeof(set));
        if (set < 0) continue;
        const int32_t id = static_cast<int32_t>(param::describe_row(table, row_number).id);
        if (!listed(id)) continue;
        if (entries >= cache.capacity) return;
        cache.ids[entries] = id;
        cache.sets[entries] = set;
        ++entries;
    }
}

constexpr int32_t bits_per_byte = 8;

int32_t check_set(const SetCache &cache, int32_t id, const uint8_t *row, size_t set_offset, uint8_t *marked)
{
    if (row == nullptr || id < 0) return -1;
    int16_t set;
    std::memcpy(&set, row + set_offset, sizeof(set));
    if (set < 0) return -1;
    int32_t result = set;
    for (int32_t entry = 0; entry < cache.capacity; ++entry) {
        if (cache.sets[entry] != set) continue;
        const uint8_t bit = static_cast<uint8_t>(1u << (entry % bits_per_byte));
        uint8_t &byte = marked[entry / bits_per_byte];
        if (cache.ids[entry] == id)
            byte |= bit;
        else if (!(byte & bit))
            result = -1;
    }
    return result;
}

constexpr int32_t goods_set_capacity = 70;
constexpr int32_t equipment_set_capacity = 50;
RT_GLOBAL(0x05993278, goods_set_cache_built, bool);
RT_GLOBAL(0x05993040, goods_set_ids, int32_t[goods_set_capacity]);
RT_GLOBAL(0x05993160, goods_set_numbers, int32_t[goods_set_capacity]);
RT_GLOBAL(0x05993798, protector_set_cache_built, bool);
RT_GLOBAL(0x05993600, protector_set_ids, int32_t[equipment_set_capacity]);
RT_GLOBAL(0x059936d0, protector_set_numbers, int32_t[equipment_set_capacity]);
RT_GLOBAL(0x05993d18, weapon_set_cache_built, bool);
RT_GLOBAL(0x05993b80, weapon_set_ids, int32_t[equipment_set_capacity]);
RT_GLOBAL(0x05993c50, weapon_set_numbers, int32_t[equipment_set_capacity]);

// Where each table keeps the set number. The attire check builds its cache from +0xd8 but reads
// the looked-up row's +0xca.
constexpr size_t goods_set_offset = 0x32;
constexpr size_t protector_cache_set_offset = 0xd8;
constexpr size_t protector_row_set_offset = 0xca;
constexpr size_t weapon_set_offset = 0xd8;
// The weapon set lists only ids that are multiples of 100000 and compares the looked-up base id
// rounded down to one.
constexpr int32_t weapon_set_id_unit = 100000;

int32_t weapon_set_id(int32_t id)
{
    return id / weapon_set_id_unit * weapon_set_id_unit;
}

const auto list_every_row = [](int32_t) { return true; };

}  // namespace

extern "C" int32_t bb_param_check_set_equip_param_goods(const RowLookup *lookup, uint8_t *marked)
{
    const SetCache cache{&goods_set_cache_built.get(), goods_set_ids.get(), goods_set_numbers.get(), goods_set_capacity};
    if (!*cache.built) build_set_cache(cache, ParamType::equip_param_goods, goods_set_offset, list_every_row);
    return check_set(cache, lookup->id, lookup->row, goods_set_offset, marked);
}

extern "C" int32_t bb_param_check_set_equip_param_protector(const EquipRowLookup *lookup, uint8_t *marked)
{
    const SetCache cache{&protector_set_cache_built.get(), protector_set_ids.get(), protector_set_numbers.get(),
                         equipment_set_capacity};
    if (!*cache.built)
        build_set_cache(cache, ParamType::equip_param_protector, protector_cache_set_offset, list_every_row);
    return check_set(cache, lookup->base_id, lookup->equip.row, protector_row_set_offset, marked);
}

extern "C" int32_t bb_param_check_set_equip_param_weapon(const EquipRowLookup *lookup, uint8_t *marked)
{
    const SetCache cache{&weapon_set_cache_built.get(), weapon_set_ids.get(), weapon_set_numbers.get(),
                         equipment_set_capacity};
    if (!*cache.built) {
        build_set_cache(cache, ParamType::equip_param_weapon, weapon_set_offset,
                        [](int32_t id) { return weapon_set_id(id) == id; });
    }
    if (lookup->base_id < 0) return -1;
    return check_set(cache, weapon_set_id(lookup->base_id), lookup->equip.row, weapon_set_offset, marked);
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
