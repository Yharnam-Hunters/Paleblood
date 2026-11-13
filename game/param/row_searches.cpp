// SPDX-License-Identifier: GPL-2.0-or-later
// Smaller per-type accessors of the solo parameter repository (param_repository.h) that don't
// share the plain row lookup's shape: the attack and behavior lookups, which pick an NPC or a
// player table; a table view; a lookup that also returns the row number; searches by row number
// or by the rows' contents; and the clamp the game applies to the chalice weapon and attire
// generation tables after they load.
#include <cstring>

#include "../engine/container.h"
#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::RowLookup;
using param::TableHeader;

constexpr int32_t no_row = -1;

// Rows are read with memcpy: the tables give no alignment guarantee.
template <typename Row> Row read_row(const uint8_t *data)
{
    Row row;
    std::memcpy(&row, data, sizeof(row));
    return row;
}

// The AtkParam and BehaviorParam lookups pick one of a pair of tables (the NPC's or the
// player's: AtkParam_Npc / AtkParam_Pc, BehaviorParam / BehaviorParam_PC) and return which one
// beside the id and the row.
struct PairedRowLookup {
    int32_t id;
    int32_t user;  // 0: NPC table, 1: player table
    const uint8_t *row;
};
static_assert(offsetof(PairedRowLookup, user) == 0x4 && offsetof(PairedRowLookup, row) == 0x8);
constexpr int32_t npc_user = 0, player_user = 1;

// The table type for a user: the pair's first type plus the user. Only the upper end of the user
// is checked (the BehaviorParam lookup also refuses a negative one); the sum is then used as an
// unsigned slot number.
void find_paired_row(PairedRowLookup *out, ParamType npc_table, int32_t user, int32_t id)
{
    const uint8_t *row = nullptr;
    out->user = user;
    if (id >= 0 && user <= player_user) {
        const auto type = static_cast<ParamType>(static_cast<int32_t>(npc_table) + user - npc_user);
        if (const TableHeader *table = loaded_table(type))
            row = param::find_row_in_table(table, static_cast<uint32_t>(id));
    }
    out->id = id;
    out->row = row;
}

// A view of one table: an object with a vtable (the game's class for it) and the table, or no
// table when it is not loaded.
struct TableView {
    const uint8_t *vtable;
    const TableHeader *table;
};
static_assert(offsetof(TableView, table) == 0x8);
RT_GLOBAL(0x05749140, table_view_vtable, const uint8_t);

// The view's type check lets through one type past the last (`type <= param_type_count`), as the
// original's does.
constexpr int32_t last_viewable_type = param::param_type_count;

// The ResidentFxParam lookup also returns the row number the index gave, and the table.
struct ResidentFxLookup {
    RowLookup lookup;
    int32_t row_number;  // -1 when the id is not found; the index's value otherwise
    uint8_t unknown_0x14[0xc];
    const TableHeader *table;  // written only when the table is loaded
};
static_assert(offsetof(ResidentFxLookup, row_number) == 0x10 && offsetof(ResidentFxLookup, table) == 0x20);

// A GemeffectParam row: only the fields the effect choice reads.
struct GemEffectRow {
    uint8_t unknown_0x00[0x4];
    int32_t group;          // the kind of effect asked for
    int32_t rank;           // the choice takes the highest rank up to the level
    int32_t lowest_level;   // the row applies from this level...
    int32_t highest_level;  // ...to this one
};
static_assert(offsetof(GemEffectRow, group) == 0x4 && offsetof(GemEffectRow, highest_level) == 0x10);

// A GemCategoryParam row: only the two fields the lists filter on (one each; their meaning is
// not known).
struct GemCategoryRow {
    uint8_t unknown_0x00[0x8];
    int32_t selector_08;
    int32_t selector_0c;
};
static_assert(offsetof(GemCategoryRow, selector_08) == 0x8 && offsetof(GemCategoryRow, selector_0c) == 0xc);

RT_ORIGINAL(0x0231c840, row_lookups_push_back, void(engine::Array<RowLookup> *, const RowLookup *lookup));

// Appends the lookup of every GemCategoryParam row for which `selected(row)` holds, in file order.
template <typename Selected> void list_gem_category_rows(engine::Array<RowLookup> *rows, const Selected &selected)
{
    const TableHeader *table = loaded_table(ParamType::gem_category);
    if (table == nullptr) return;
    // The count is read once; each row's data is used as it is, without a check.
    const uint32_t count = table->row_count;
    for (uint32_t row_number = 0; row_number < count; ++row_number) {
        const param::RowDescription description = param::describe_row(table, row_number);
        const auto row = read_row<GemCategoryRow>(param::at_offset(table, description.data_offset));
        if (!selected(row)) continue;
        RowLookup lookup{};
        lookup.id = no_row;
        lookup.row = nullptr;
        param::find_row(&lookup, static_cast<int32_t>(description.id), ParamType::gem_category);
        row_lookups_push_back(rows, &lookup);
    }
}

// ProtectorGenParam and WeaponGenParam rows start with five entries, each a value and an index
// into a set of 64 (a bit number); after loading, the game marks an index past the set as none.
struct GenerationEntry {
    uint32_t value;
    uint32_t index;
};
constexpr size_t generation_entry_count = 5;
struct ProtectorGenRow {
    GenerationEntry entries[generation_entry_count];
    uint8_t unknown_0x28[0x4];
};
static_assert(sizeof(ProtectorGenRow) == 0x2c && offsetof(ProtectorGenRow, entries[1].index) == 0xc);
struct WeaponGenRow {
    GenerationEntry entries[generation_entry_count];
    uint8_t unknown_0x28[0x8];
};
static_assert(sizeof(WeaponGenRow) == 0x30 && offsetof(WeaponGenRow, entries[1].index) == 0xc);
constexpr uint32_t largest_index = 63;
constexpr uint32_t no_index = 0x80000000;

// The table is changed in place. The rows are taken to follow each other from the first row's
// data on, as the loader lays them out; the row descriptors after the first are not read.
template <typename Row> void clamp_generation_indexes(ParamType type)
{
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return;
    const uint32_t count = table->row_count;
    if (count == 0) return;
    auto *rows = reinterpret_cast<uint8_t *>(
        reinterpret_cast<uintptr_t>(table) + param::describe_row(table, 0).data_offset);
    for (uint32_t row_number = 0; row_number < count; ++row_number) {
        uint8_t *data = rows + sizeof(Row) * row_number;
        for (size_t entry = 0; entry < generation_entry_count; ++entry) {
            const size_t at = offsetof(Row, entries) + sizeof(GenerationEntry) * entry +
                              offsetof(GenerationEntry, index);
            uint32_t index;
            std::memcpy(&index, data + at, sizeof(index));
            if (index > largest_index) index = no_index;
            std::memcpy(data + at, &index, sizeof(index));
        }
    }
}

}  // namespace

// The attack (AtkParam) row of an NPC (user 0) or the player (1). A negative id, or a user
// above 1, gives no row without reading the repository; a negative user reads a table type
// before AtkParam_Npc (-1: NpcParam), as in the original.
extern "C" void bb_param_find_row_atk_param(PairedRowLookup *out, int32_t user, int32_t id)
{
    find_paired_row(out, ParamType::atk_param_npc, user, id);
}

// The behavior (BehaviorParam) row of an NPC (user 0) or the player (1); takes the id first, and
// a negative user gives no row.
extern "C" void bb_param_find_row_behavior_param(PairedRowLookup *out, int32_t id, int32_t user)
{
    if (user < npc_user) {
        out->user = user;
        out->id = id;
        out->row = nullptr;
        return;
    }
    find_paired_row(out, ParamType::behavior, user, id);
}

// The repository is required first; the type is then checked as a signed number but used as an
// unsigned one, as in the original.
extern "C" TableView *bb_param_table_view(TableView *out, int32_t type)
{
    const param::SoloParamRepository *repository =
        engine::require(param::solo_param_repository, param::solo_param_repository_name.address());
    const TableHeader *table = nullptr;
    if (type <= last_viewable_type) {
        const param::ParamSlot &slot = repository->slots[static_cast<uint32_t>(type)];
        if (slot.loaded != 0 && slot.holder != nullptr) table = slot.holder->file->table;
    }
    out->vtable = table_view_vtable.address();
    out->table = table;
    return out;
}

// The ResidentFxParam row of an id, with the row number the index gives it. A row number past
// the count, or negative, is returned with no row.
extern "C" void bb_param_find_row_resident_fx(ResidentFxLookup *out, int32_t id)
{
    const uint8_t *row = nullptr;
    int32_t row_number = no_row;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(ParamType::resident_fx)) {
            out->table = table;
            row_number = param::find_row_number(table, static_cast<uint32_t>(id));
            if (row_number >= 0) row = param::row_data_if_present(table, static_cast<uint32_t>(row_number));
        }
    }
    out->lookup.id = id;
    out->lookup.row = row;
    out->row_number = row_number;
}

// The GemeffectParam row for a group at a level: of the rows of that group whose level range
// holds the level, the one with the highest rank that is not above the level (the first of
// equals, and only ranks of 0 and up). Returns its id, or -1.
extern "C" int32_t bb_param_gem_effect_for_level(int32_t level, int32_t group)
{
    const TableHeader *table = loaded_table(ParamType::gem_effect);
    if (table == nullptr) return no_row;
    const uint32_t count = table->row_count;
    int32_t best_rank = -1;
    uint32_t best = count;
    for (uint32_t row_number = 0; row_number < count; ++row_number) {
        const auto row = read_row<GemEffectRow>(param::row_data(table, row_number));
        if (row.group != group || row.lowest_level > level || row.highest_level < level) continue;
        if (row.rank > best_rank && row.rank <= level) {
            best_rank = row.rank;
            best = row_number;
        }
    }
    if (best >= count) return no_row;
    return static_cast<int32_t>(param::describe_row(table, best).id);
}

// The number of the first GemGenParam row whose id is at least `id` (ids compare unsigned), or
// -1. A negative id gives -1 without reading the repository.
extern "C" int32_t bb_param_gem_gen_first_row_from(int32_t id)
{
    if (id < 0) return no_row;
    const TableHeader *table = loaded_table(ParamType::gem_gen);
    if (table == nullptr) return no_row;
    const uint32_t count = table->row_count;
    for (uint32_t row_number = 0; row_number < count; ++row_number) {
        if (param::describe_row(table, row_number).id >= static_cast<uint32_t>(id))
            return static_cast<int32_t>(row_number);
    }
    return no_row;
}

// The number of the last GemGenParam row whose id is at most `id`, or -1.
extern "C" int32_t bb_param_gem_gen_last_row_to(int32_t id)
{
    if (id < 0) return no_row;
    const TableHeader *table = loaded_table(ParamType::gem_gen);
    if (table == nullptr) return no_row;
    for (uint32_t row_number = table->row_count; row_number-- > 0;) {
        if (param::describe_row(table, row_number).id <= static_cast<uint32_t>(id))
            return static_cast<int32_t>(row_number);
    }
    return no_row;
}

// The id of GemGenParam row `row_number`. The row number is not checked against the count.
extern "C" int32_t bb_param_gem_gen_row_id(int32_t row_number)
{
    if (row_number < 0) return no_row;
    const TableHeader *table = loaded_table(ParamType::gem_gen);
    if (table == nullptr) return no_row;
    return static_cast<int32_t>(param::describe_row(table, static_cast<uint32_t>(row_number)).id);
}

// The HolygrailExParam row number the index gives the lookup's id (negative ids are searched
// too), or -1.
extern "C" int32_t bb_param_holygrail_ex_row_number(const RowLookup *lookup)
{
    const TableHeader *table = loaded_table(ParamType::holygrail_ex);
    if (table == nullptr) return no_row;
    return param::find_row_number(table, static_cast<uint32_t>(lookup->id));
}

// HolygrailExParam row `row_number`, looked up again by its id (so a row the index doesn't list
// is not found): id -1 and no row when the table is not loaded or has fewer rows.
extern "C" void bb_param_find_row_number_holygrail_ex(RowLookup *out, uint32_t row_number)
{
    out->id = no_row;
    out->row = nullptr;
    const TableHeader *table = loaded_table(ParamType::holygrail_ex);
    if (table == nullptr || row_number >= table->row_count) return;
    const int32_t id = static_cast<int32_t>(param::describe_row(table, row_number).id);
    param::find_row(out, id, ParamType::holygrail_ex);
}

// Append the row lookup of every GemCategoryParam row whose field matches `value`, in file order.
extern "C" void bb_param_list_gem_category_by_selector_0c(engine::Array<RowLookup> *rows, int32_t value)
{
    list_gem_category_rows(rows, [value](const GemCategoryRow &row) { return row.selector_0c == value; });
}

extern "C" void bb_param_list_gem_category_by_selector_08(engine::Array<RowLookup> *rows, int32_t value)
{
    list_gem_category_rows(rows, [value](const GemCategoryRow &row) { return row.selector_08 == value; });
}

extern "C" void bb_param_clamp_protector_gen_indexes()
{
    clamp_generation_indexes<ProtectorGenRow>(ParamType::protector_gen);
}

extern "C" void bb_param_clamp_weapon_gen_indexes()
{
    clamp_generation_indexes<WeaponGenRow>(ParamType::weapon_gen);
}
