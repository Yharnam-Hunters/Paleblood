// SPDX-License-Identifier: GPL-2.0-or-later
// Two more per-type accessor templates of the solo parameter repository (param_repository.h),
// compiled once per table type like the row lookups of find_row.cpp:
// - the row name lookups: the name the PARAM file gives the row a lookup found (row names are
//   the editor's labels, kept as text after the rows), or null;
// - the row-at accessors: row number `n` of a table, with its id, for code that walks a table in
//   file order instead of by id.
#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::RowLookup;
using param::TableHeader;

// The name of the row whose id `lookup` holds (a lookup's result, or one filled in by hand).
// A negative id gives null without reading the repository.
const char *find_row_name(const RowLookup *lookup, ParamType type)
{
    if (lookup->id < 0) return nullptr;
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return nullptr;
    // The id is read again: the missing-repository report above may run in between.
    return param::find_row_name_in_table(table, static_cast<uint32_t>(lookup->id));
}

// Row `row_number` of the table and its id; id -1 and no row when the table is not loaded or
// has fewer rows.
void find_row_at(RowLookup *out, uint32_t row_number, ParamType type)
{
    int32_t id = -1;
    const uint8_t *row = nullptr;
    if (const TableHeader *table = loaded_table(type)) {
        if (row_number < table->row_count) {
            const param::RowDescription description = param::describe_row(table, row_number);
            id = static_cast<int32_t>(description.id);
            row = param::at_offset(table, description.data_offset);
        }
    }
    out->id = id;
    out->row = row;
}

}  // namespace

extern "C" const char *bb_param_row_name_equip_param_weapon(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::equip_param_weapon);
}

extern "C" const char *bb_param_row_name_equip_param_protector(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::equip_param_protector);
}

extern "C" const char *bb_param_row_name_equip_param_accessory(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::equip_param_accessory);
}

extern "C" const char *bb_param_row_name_equip_param_goods(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::equip_param_goods);
}

extern "C" const char *bb_param_row_name_chara_init(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::chara_init);
}

extern "C" const char *bb_param_row_name_face_gen(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::face_gen);
}

extern "C" const char *bb_param_row_name_weapon_gen(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::weapon_gen);
}

extern "C" const char *bb_param_row_name_gem_gen(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::gem_gen);
}

// A second compiled copy for the same table, as with the GemGenParam row lookup.
extern "C" const char *bb_param_row_name_gem_gen_2(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::gem_gen);
}

extern "C" const char *bb_param_row_name_gem_effect(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::gem_effect);
}

extern "C" const char *bb_param_row_name_holygrail_ex(const RowLookup *lookup)
{
    return find_row_name(lookup, ParamType::holygrail_ex);
}

extern "C" void bb_param_row_at_equip_param_goods(RowLookup *out, uint32_t row_number)
{
    find_row_at(out, row_number, ParamType::equip_param_goods);
}

extern "C" void bb_param_row_at_magic(RowLookup *out, uint32_t row_number)
{
    find_row_at(out, row_number, ParamType::magic);
}

extern "C" void bb_param_row_at_menu_color_table(RowLookup *out, uint32_t row_number)
{
    find_row_at(out, row_number, ParamType::menu_color_table);
}

extern "C" void bb_param_row_at_chara_init(RowLookup *out, uint32_t row_number)
{
    find_row_at(out, row_number, ParamType::chara_init);
}
