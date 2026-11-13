// SPDX-License-Identifier: GPL-2.0-or-later
// Accessors that walk a table of the solo parameter repository (param_repository.h) in file
// order rather than look one id up, compiled once per table type like the row lookups of
// find_row.cpp. The equipment and item tables (weapons, attire, badges, items, hunter tools) are
// the ones the game walks this way: these are what an inventory or shop list needs to page
// through every row of a table.
// - the row count of a table;
// - the id of the row after or before the row with a given id (file order: PARAM files keep
//   their rows sorted by id, so this is the next or previous id), which steps through a table;
// - every row id of a table, appended to an id array.
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
