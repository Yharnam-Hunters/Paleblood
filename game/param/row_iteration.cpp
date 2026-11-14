// SPDX-License-Identifier: GPL-2.0-or-later
// Accessors that walk a table of the solo parameter repository (param_repository.h) in file
// order rather than look one id up, compiled once per table type like the row lookups of
// find_row.cpp. The equipment and item tables (weapons, attire, badges, items, hunter tools) are
// the ones the game walks this way: these are what an inventory or shop list needs to page
// through every row of a table.
// - the row count of a table;
// - the id of the row after or before the row with a given id (file order: PARAM files keep
//   their rows sorted by id, so this is the next or previous id), which steps through a table;
// - every row id of a table, appended to an id array;
// - the same steps and lists restricted to rows with a given field value, and lists that add each
//   row's reinforced versions after it.
#include <cstring>

#include "../engine/container.h"
#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::TableHeader;

// What the stepping accessors return when there is no such row.
constexpr int32_t no_row_id = -1;

// The id of row `row_number` (which the caller has checked against the count).
int32_t row_id(const TableHeader *table, uint32_t row_number)
{
    return static_cast<int32_t>(param::describe_row(table, row_number).id);
}

// The number of rows in the table, or 0 when it is not loaded.
uint32_t row_count(ParamType type)
{
    const TableHeader *table = loaded_table(type);
    return table == nullptr ? 0 : table->row_count;
}

// The id of the row after the row with id `id`; a negative id asks for the first row. -1 when the
// table is not loaded or empty, has no row with that id, or that row is the last. Unlike the row
// lookups, a negative id still reads the repository.
int32_t next_row_id(ParamType type, int32_t id)
{
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return no_row_id;
    if (id < 0) return table->row_count == 0 ? no_row_id : row_id(table, 0);
    const int32_t row_number = param::find_row_number(table, static_cast<uint32_t>(id));
    if (row_number < 0) return no_row_id;
    const uint32_t next = static_cast<uint32_t>(row_number) + 1;
    if (next >= table->row_count) return no_row_id;
    return row_id(table, next);
}

// The id of the row before the row with id `id`; a negative id asks for the last row. -1 when
// the table is not loaded or empty, has no row with that id, or that row is the first.
int32_t previous_row_id(ParamType type, int32_t id)
{
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return no_row_id;
    if (id < 0) return table->row_count == 0 ? no_row_id : row_id(table, table->row_count - 1u);
    const int32_t row_number = param::find_row_number(table, static_cast<uint32_t>(id));
    if (row_number <= 0) return no_row_id;
    const uint32_t previous = static_cast<uint32_t>(row_number) - 1;
    if (previous >= table->row_count) return no_row_id;
    return row_id(table, previous);
}

// Two compiled copies of the engine's int32 array push_back (the same code); each list uses the
// one its table's code was built with.
RT_ORIGINAL(0x02bc1bb0, id_array_push_back, void(engine::Array<int32_t> *, const int32_t *id));
RT_ORIGINAL(0x016da6a0, id_array_push_back_copy, void(engine::Array<int32_t> *, const int32_t *id));

// An EquipParamAccessory row: only the field the badge list reads.
struct AccessoryRow {
    uint8_t unknown_0x00[0x26];
    int16_t list_order;  // meaning unknown beyond this: rows where it is negative are not listed
};
static_assert(offsetof(AccessoryRow, list_order) == 0x26);

// How many ids the array holds, computed as the original does: its end minus its start in bytes,
// shifted right as an unsigned number (whatever the two pointers hold).
constexpr unsigned id_size_shift = 2;  // log2(sizeof(int32_t))

uint64_t id_count(const engine::Array<int32_t> *ids)
{
    const uint64_t bytes = reinterpret_cast<uintptr_t>(ids->end) - reinterpret_cast<uintptr_t>(ids->begin);
    return bytes >> id_size_shift;
}

// Empties `ids`, then appends the id of every row of the table, in file order, that `include`
// accepts (given the row's data); returns how many ids the array then holds. The loop runs to the
// count the table had at the start, and checks each row number against the count as it is then
// (the push_back may change it); a row past the count, or whose data is at address zero, is
// skipped.
template <typename Include, typename PushBack>
uint64_t list_row_ids(engine::Array<int32_t> *ids, ParamType type, const Include &include, const PushBack &push_back)
{
    ids->end = ids->begin;
    if (const TableHeader *table = loaded_table(type)) {
        const int32_t count = table->row_count;
        for (int32_t row_number = 0; row_number < count; ++row_number) {
            const uint8_t *data = param::row_data_if_present(table, static_cast<uint32_t>(row_number));
            if (data == nullptr || !include(data)) continue;
            const int32_t id = row_id(table, static_cast<uint32_t>(row_number));
            push_back(ids, &id);
        }
    }
    return id_count(ids);
}

template <typename PushBack>
uint64_t list_all_row_ids(engine::Array<int32_t> *ids, ParamType type, const PushBack &push_back)
{
    return list_row_ids(ids, type, [](const uint8_t *) { return true; }, push_back);
}


// Reads a field of a row (tables give no alignment guarantee).
template <typename Field> Field row_field(const uint8_t *data, size_t offset)
{
    Field value;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

// The id of the row after the row with id `id` that `accept` takes (given the row's data); a
// negative id, or one the table doesn't have, starts at the first row. -1 when there is none, or
// when the table is not loaded (the repository is required even for a negative id). A row number
// the index gives below -1 ends the search at once.
template <typename Accept> int32_t next_accepted_row_id(ParamType type, int32_t id, const Accept &accept)
{
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return no_row_id;
    const int32_t row_number = id < 0 ? -1 : param::find_row_number(table, static_cast<uint32_t>(id));
    for (uint32_t next = static_cast<uint32_t>(row_number) + 1; next < table->row_count; ++next) {
        if (accept(param::row_data(table, next))) return row_id(table, next);
    }
    return no_row_id;
}

// The id of the row before the row with id `id` that `accept` takes; a negative id starts after
// the last row. -1 when there is none, the id is not in the table, or the table is not loaded.
// A row the index places past the count is searched from there down, its descriptors read as
// they are, but a match past the count gives -1.
template <typename Accept> int32_t previous_accepted_row_id(ParamType type, int32_t id, const Accept &accept)
{
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return no_row_id;
    int32_t row_number = id < 0 ? table->row_count : param::find_row_number(table, static_cast<uint32_t>(id));
    while (row_number > 0) {
        --row_number;
        if (!accept(param::row_data(table, static_cast<uint32_t>(row_number)))) continue;
        if (static_cast<uint32_t>(row_number) >= table->row_count) return no_row_id;
        return row_id(table, static_cast<uint32_t>(row_number));
    }
    return no_row_id;
}

// An EquipParamProtector row: only the byte the filtered steps compare (its meaning is not known)
// and the reinforcement entries the variant list reads.
constexpr size_t protector_step_byte = 0xd2;
constexpr size_t protector_origins = 0x40;
// An EquipParamWeapon row: the byte its variant list filters on, its reinforcement entries, and
// the word the id list can filter on (meanings not known beyond what these lists do with them).
constexpr size_t weapon_list_byte = 0xe2;
constexpr size_t weapon_origins = 0x60;
constexpr size_t weapon_list_word = 0x4;
constexpr int32_t weapon_unlisted_word = -1;

// The equipment rows hold sixteen 32-bit reinforcement entries, one per upgrade level (each the
// id an upgrade starts from, judging by their use; -1 when there is none). The variant lists
// count the leading entries that are not negative, looking at most at the first fifteen.
constexpr int32_t origins_checked = 15;

int32_t reinforced_version_count(const uint8_t *origins)
{
    int32_t count = 0;
    while (count < origins_checked && row_field<int32_t>(origins, sizeof(int32_t) * count) >= 0) ++count;
    return count;
}

// Appends `first` and the `versions` ids after it (wrapping around as 32-bit numbers).
template <typename PushBack>
void push_versions(engine::Array<int32_t> *ids, int32_t first, int32_t versions, const PushBack &push_back)
{
    for (int32_t k = 0; k <= versions; ++k) {
        const int32_t id = static_cast<int32_t>(static_cast<uint32_t>(first) + static_cast<uint32_t>(k));
        push_back(ids, &id);
    }
}

RT_ORIGINAL(0x019f8d70, protector_id_array_push_back, void(engine::Array<int32_t> *, const int32_t *id));
RT_ORIGINAL(0x01b0d5f0, weapon_id_array_push_back, void(engine::Array<int32_t> *, const int32_t *id));

}  // namespace

extern "C" uint32_t bb_param_row_count_chara_init()
{
    return row_count(ParamType::chara_init);
}

extern "C" uint32_t bb_param_row_count_equip_param_goods()
{
    return row_count(ParamType::equip_param_goods);
}

extern "C" uint32_t bb_param_row_count_magic()
{
    return row_count(ParamType::magic);
}

extern "C" uint32_t bb_param_row_count_holygrail_ex()
{
    return row_count(ParamType::holygrail_ex);
}

extern "C" int32_t bb_param_next_row_id_equip_param_accessory(int32_t id)
{
    return next_row_id(ParamType::equip_param_accessory, id);
}

extern "C" int32_t bb_param_next_row_id_equip_param_goods(int32_t id)
{
    return next_row_id(ParamType::equip_param_goods, id);
}

extern "C" int32_t bb_param_next_row_id_magic(int32_t id)
{
    return next_row_id(ParamType::magic, id);
}

extern "C" int32_t bb_param_next_row_id_equip_param_protector(int32_t id)
{
    return next_row_id(ParamType::equip_param_protector, id);
}

extern "C" int32_t bb_param_previous_row_id_equip_param_accessory(int32_t id)
{
    return previous_row_id(ParamType::equip_param_accessory, id);
}

extern "C" int32_t bb_param_previous_row_id_equip_param_goods(int32_t id)
{
    return previous_row_id(ParamType::equip_param_goods, id);
}

extern "C" int32_t bb_param_previous_row_id_magic(int32_t id)
{
    return previous_row_id(ParamType::magic, id);
}

extern "C" int32_t bb_param_previous_row_id_equip_param_protector(int32_t id)
{
    return previous_row_id(ParamType::equip_param_protector, id);
}

extern "C" uint64_t bb_param_list_row_ids_equip_param_accessory(engine::Array<int32_t> *ids)
{
    return list_all_row_ids(ids, ParamType::equip_param_accessory, id_array_push_back);
}

extern "C" uint64_t bb_param_list_row_ids_equip_param_goods(engine::Array<int32_t> *ids)
{
    return list_all_row_ids(ids, ParamType::equip_param_goods, id_array_push_back);
}

extern "C" uint64_t bb_param_list_row_ids_equip_param_protector(engine::Array<int32_t> *ids)
{
    return list_all_row_ids(ids, ParamType::equip_param_protector, id_array_push_back_copy);
}

// The badges list: accessory rows whose list order is negative are left out.
extern "C" uint64_t bb_param_list_listed_ids_equip_param_accessory(engine::Array<int32_t> *ids)
{
    const auto listed = [](const uint8_t *data) {
        int16_t order;
        std::memcpy(&order, data + offsetof(AccessoryRow, list_order), sizeof(order));
        return order >= 0;
    };
    return list_row_ids(ids, ParamType::equip_param_accessory, listed, id_array_push_back_copy);
}

// The EquipParamProtector row after (or before) the row with id `id` whose byte at +0xd2 equals
// `value` (a value outside 0-255 never matches).
extern "C" int32_t bb_param_next_matching_id_equip_param_protector(int32_t id, int32_t value)
{
    return next_accepted_row_id(ParamType::equip_param_protector, id, [value](const uint8_t *data) {
        return static_cast<int32_t>(data[protector_step_byte]) == value;
    });
}

extern "C" int32_t bb_param_previous_matching_id_equip_param_protector(int32_t id, int32_t value)
{
    return previous_accepted_row_id(ParamType::equip_param_protector, id, [value](const uint8_t *data) {
        return static_cast<int32_t>(data[protector_step_byte]) == value;
    });
}

// Empties `ids` and appends, for every EquipParamProtector row in file order, its id and the ids
// of its reinforced versions after it. A row whose data is at address zero adds only its id; a
// row past the count as it is then adds -1. Returns how many ids the array holds.
extern "C" uint64_t bb_param_list_versions_equip_param_protector(engine::Array<int32_t> *ids)
{
    ids->end = ids->begin;
    if (const TableHeader *table = loaded_table(ParamType::equip_param_protector)) {
        const int32_t count = table->row_count;
        for (int32_t row_number = 0; row_number < count; ++row_number) {
            if (static_cast<uint32_t>(row_number) >= table->row_count) {
                push_versions(ids, no_row_id, 0, protector_id_array_push_back);
                continue;
            }
            const uint8_t *data = param::row_data(table, static_cast<uint32_t>(row_number));
            const int32_t versions = data == nullptr ? 0 : reinforced_version_count(data + protector_origins);
            push_versions(ids, row_id(table, static_cast<uint32_t>(row_number)), versions,
                          protector_id_array_push_back);
        }
    }
    return id_count(ids);
}

// The same for the EquipParamWeapon rows whose byte at +0xe2 equals `value`; rows past the
// count, or whose data is at address zero, are skipped.
extern "C" uint64_t bb_param_list_versions_equip_param_weapon(engine::Array<int32_t> *ids, int32_t value)
{
    ids->end = ids->begin;
    if (const TableHeader *table = loaded_table(ParamType::equip_param_weapon)) {
        const int32_t count = table->row_count;
        for (int32_t row_number = 0; row_number < count; ++row_number) {
            const uint8_t *data = param::row_data_if_present(table, static_cast<uint32_t>(row_number));
            if (data == nullptr || static_cast<int32_t>(data[weapon_list_byte]) != value) continue;
            push_versions(ids, row_id(table, static_cast<uint32_t>(row_number)),
                          reinforced_version_count(data + weapon_origins), weapon_id_array_push_back);
        }
    }
    return id_count(ids);
}

// Every EquipParamWeapon row id; with `only_listed` (its low byte), rows whose word at +0x4 is -1
// are left out.
extern "C" uint64_t bb_param_list_row_ids_equip_param_weapon(engine::Array<int32_t> *ids, uint32_t only_listed)
{
    const bool filter = static_cast<uint8_t>(only_listed) != 0;
    const auto listed = [filter](const uint8_t *data) {
        return !filter || row_field<int32_t>(data, weapon_list_word) != weapon_unlisted_word;
    };
    return list_row_ids(ids, ParamType::equip_param_weapon, listed, id_array_push_back_copy);
}

// Two id lists whose walk the compiler removed: each empties the array, requires the repository
// and returns the array's count.
static uint64_t clear_id_list(engine::Array<int32_t> *ids)
{
    ids->end = ids->begin;
    engine::require(param::solo_param_repository, param::solo_param_repository_name.address());
    return id_count(ids);
}

extern "C" uint64_t bb_param_clear_id_list_equip_param_protector(engine::Array<int32_t> *ids)
{
    return clear_id_list(ids);
}

extern "C" uint64_t bb_param_clear_id_list_equip_param_weapon(engine::Array<int32_t> *ids)
{
    return clear_id_list(ids);
}
