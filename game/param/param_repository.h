// SPDX-License-Identifier: GPL-2.0-or-later
// The solo parameter repository: the game's parameter tables (PARAM files), one slot per table
// type, and the row lookup every per-type accessor shares.
//
// A PARAM file is a header, a list of row descriptors (row id, offset of the row's data) and the
// rows' data; the layout is the one the modding community documents for FromSoftware's PARAM
// files. Bloodborne's loader keeps a search index after each loaded file: (row id, row number)
// pairs sorted by id, at the file's size rounded up to 16 bytes from its header. The size is the
// word 16 bytes before the header.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"
#include "../engine/engine.h"

namespace param {

// The table types, in the order of the repository's type table (each entry a name and this
// index; the names are the community's names for these tables). A type's slot is
// SoloParamRepository::slots[type].
enum class ParamType : int32_t {
    equip_param_weapon = 0,
    equip_param_protector = 1,
    equip_param_accessory = 2,
    equip_param_goods = 3,
    reinforce_param_weapon = 4,
    reinforce_param_protector = 5,
    npc = 6,
    atk_param_npc = 7,
    atk_param_pc = 8,
    npc_think = 9,
    object = 10,
    bullet = 11,
    behavior = 12,
    behavior_pc = 13,
    magic = 14,
    sp_effect = 15,
    sp_effect_vfx = 16,
    talk = 17,
    menu_color_table = 18,
    item_lot = 19,
    move = 20,
    chara_init = 21,
    equip_mtrl_set = 22,
    face_gen = 23,
    face = 24,
    face_range = 25,
    ragdoll = 26,
    shop_lineup = 27,
    qwc_change = 28,
    qwc_judge = 29,
    game_area = 30,
    skeleton = 31,
    calc_correct_graph = 32,
    lock_cam = 33,
    obj_act = 34,
    hit_mtrl = 35,
    knock_back = 36,
    wind = 37,
    decal = 38,
    action_button = 39,
    weapon_gen = 40,
    protector_gen = 41,
    gem_gen = 42,
    gem_effect = 43,
    gem_category = 44,
    gem_drop_doping = 45,
    gem_drop_modify = 46,
    resident_fx = 47,
    ai_sound = 48,
    game_map = 49,
    return_point = 50,
    item_lot_lvdep = 51,
    holygrail_ex = 52,
    dungeon_feature = 53,
    dungeon_sub_feat_lot = 54,
    ritual_required_mat = 55,
    char_make_menu_top = 56,
    char_make_menu_list_item = 57,
    new_menu_color_table = 58,
    menu_property_spec = 59,
    menu_property_layout = 60,
    menu_value_table = 61,
};
constexpr int32_t param_type_count = 62;

// The start of a PARAM file: only the fields the lookup reads.
struct TableHeader {
    uint8_t unknown_0x00[0x6];
    uint16_t header_word_06;  // meaning unknown; the DungeonFeatureParam lookup publishes it
    uint16_t data_version;    // version of the file's row layout
    uint16_t row_count;
    uint8_t unknown_0x0c[0x21];
    uint8_t format;        // which row-descriptor layout follows the header (below)
    uint8_t format_flags;  // bit 1: 64-bit data offsets, from format 4 on
};
static_assert(offsetof(TableHeader, header_word_06) == 0x6);
static_assert(offsetof(TableHeader, data_version) == 0x8);
static_assert(offsetof(TableHeader, row_count) == 0xa);
static_assert(offsetof(TableHeader, format) == 0x2d);
static_assert(offsetof(TableHeader, format_flags) == 0x2e);

// Format 2 puts 32-bit row descriptors right after a 0x30-byte header; later formats use a
// 0x40-byte header, and from format 4 on a flag selects 64-bit descriptors.
constexpr uint8_t compact_header_format = 2;
constexpr uint8_t first_wide_capable_format = 4;
constexpr uint8_t wide_offsets_flag = 0x2;
constexpr size_t compact_header_size = 0x30;
constexpr size_t header_size = 0x40;

struct RowDescriptor32 {
    uint32_t id;
    uint32_t data_offset;  // from the header
    uint32_t name_offset;
};
static_assert(sizeof(RowDescriptor32) == 0xc && offsetof(RowDescriptor32, data_offset) == 0x4);

struct RowDescriptor64 {
    uint32_t id;
    uint8_t unknown_0x04[0x4];
    uint64_t data_offset;  // from the header
    uint64_t name_offset;
};
static_assert(sizeof(RowDescriptor64) == 0x18 && offsetof(RowDescriptor64, data_offset) == 0x8);

// The loader's search index entry, after the file.
struct IndexEntry {
    uint32_t id;
    int32_t row_number;  // negative: no row
};
static_assert(sizeof(IndexEntry) == 8 && offsetof(IndexEntry, row_number) == 4);

// The word before the header (16 bytes before it) holds the file's size.
constexpr ptrdiff_t stored_size_distance = 0x10;
constexpr uint32_t index_alignment = 0x10;

struct ParamFile {
    uint8_t unknown_0x00[0x70];
    const TableHeader *table;
};
static_assert(offsetof(ParamFile, table) == 0x70);

// The resource that owns a loaded table.
struct ParamHolder {
    uint8_t unknown_0x00[0x70];
    const ParamFile *file;
};
static_assert(offsetof(ParamHolder, file) == 0x70);

struct ParamSlot {
    uint32_t loaded;  // zero until the type's table is loaded
    uint8_t unknown_0x04[0x4];
    const ParamHolder *holder;
    uint8_t unknown_0x10[0x38];
};
static_assert(sizeof(ParamSlot) == 0x48 && offsetof(ParamSlot, holder) == 0x8);

struct SoloParamRepository {
    uint8_t unknown_0x00[0x70];
    ParamSlot slots[param_type_count];
};
static_assert(offsetof(SoloParamRepository, slots) == 0x70);

RT_GLOBAL(0x05940340, solo_param_repository, SoloParamRepository *);
RT_GLOBAL(0x04d38ad0, solo_param_repository_name, const char);

// What a row lookup returns through its first argument: the id asked for and the row's data
// (null when there is none). The four bytes between them are left as they were.
struct RowLookup {
    int32_t id;
    uint8_t unknown_0x04[0x4];
    const uint8_t *row;
};
static_assert(offsetof(RowLookup, row) == 0x8);

// Row descriptor `row_number`'s fields, from whichever descriptor layout the table uses.
struct RowDescription {
    uint32_t id;
    uint64_t data_offset;  // from the header
    uint64_t name_offset;  // from the header; zero when the row has no name
};

inline RowDescription describe_row(const TableHeader *table, uint32_t row_number)
{
    const auto *base = reinterpret_cast<const uint8_t *>(table);
    if (table->format == compact_header_format) {
        const RowDescriptor32 &row = reinterpret_cast<const RowDescriptor32 *>(base + compact_header_size)[row_number];
        return {row.id, row.data_offset, row.name_offset};
    }
    if (table->format < first_wide_capable_format || (table->format_flags & wide_offsets_flag) == 0) {
        const RowDescriptor32 &row = reinterpret_cast<const RowDescriptor32 *>(base + header_size)[row_number];
        return {row.id, row.data_offset, row.name_offset};
    }
    const RowDescriptor64 &row = reinterpret_cast<const RowDescriptor64 *>(base + header_size)[row_number];
    return {row.id, row.data_offset, row.name_offset};
}

// The address `offset` bytes from the header. A 64-bit offset may wrap around: the address
// arithmetic is unsigned, as in the original.
inline const uint8_t *at_offset(const TableHeader *table, uint64_t offset)
{
    return reinterpret_cast<const uint8_t *>(reinterpret_cast<uintptr_t>(table) + offset);
}

// The data of row `row_number` of a table.
inline const uint8_t *row_data(const TableHeader *table, uint32_t row_number)
{
    return at_offset(table, describe_row(table, row_number).data_offset);
}

// The data of row `row_number`, or null when the table has no such row. The ReturnPointParam
// scans read every row through this, so a row number past the count gives null.
inline const uint8_t *row_data_if_present(const TableHeader *table, uint32_t row_number)
{
    if (row_number >= table->row_count) return nullptr;
    return row_data(table, row_number);
}

// The search index after a table.
inline const IndexEntry *index_of(const TableHeader *table)
{
    const auto *base = reinterpret_cast<const uint8_t *>(table);
    const uint32_t size = *reinterpret_cast<const uint32_t *>(base - stored_size_distance);
    const uint32_t rounded = (size + index_alignment - 1) & ~(index_alignment - 1);
    // The rounded size is used as a signed 32-bit distance (a size of 2 GiB or more points back).
    return reinterpret_cast<const IndexEntry *>(base + static_cast<int32_t>(rounded));
}

// Binary search of the index for `id` (ids compare unsigned): the row number the index gives it
// (negative when the entry has no row), or -1 when the id is not in the index.
inline int32_t find_row_number(const TableHeader *table, uint32_t id)
{
    const IndexEntry *index = index_of(table);
    int32_t low = 0;
    int32_t high = table->row_count - 1;
    while (low <= high) {
        // Both bounds stay within a 16-bit row count, so the sum cannot overflow.
        const int32_t middle = (low + high) >> 1;
        const IndexEntry &entry = index[middle];
        if (entry.id == id) return entry.row_number;
        if (entry.id < id)
            low = middle + 1;
        else
            high = middle - 1;
    }
    return -1;
}

// The data of the row with id `id`, or null.
inline const uint8_t *find_row_in_table(const TableHeader *table, uint32_t id)
{
    const int32_t row_number = find_row_number(table, id);
    if (row_number < 0) return nullptr;
    return row_data(table, static_cast<uint32_t>(row_number));
}

// The name of the row with id `id` (text the file keeps after the rows), or null when there is
// no such row or it has no name. Unlike the data lookups, this one checks the index's row
// number against the row count.
inline const char *find_row_name_in_table(const TableHeader *table, uint32_t id)
{
    const int32_t row_number = find_row_number(table, id);
    if (row_number < 0 || static_cast<uint32_t>(row_number) >= table->row_count) return nullptr;
    const uint64_t name_offset = describe_row(table, static_cast<uint32_t>(row_number)).name_offset;
    if (name_offset == 0) return nullptr;
    return reinterpret_cast<const char *>(at_offset(table, name_offset));
}

// Lookups for a few types also return the low byte of the table's data version (when the table
// is loaded).
struct VersionedRowLookup {
    RowLookup lookup;
    uint8_t data_version;
};
static_assert(offsetof(VersionedRowLookup, data_version) == 0x10);

// The NpcThinkParam lookup takes two more words, which it stores beside the result unread.
struct NpcThinkRowLookup {
    RowLookup lookup;
    int32_t passed_word_0;
    int32_t passed_word_1;
};
static_assert(offsetof(NpcThinkRowLookup, passed_word_0) == 0x10);
static_assert(offsetof(NpcThinkRowLookup, passed_word_1) == 0x14);

// The table of `type`, or null when it is not loaded. The repository must exist: a missing one
// is reported, and the lookup then reads it anyway, as the game does.
inline const TableHeader *loaded_table(ParamType type)
{
    const SoloParamRepository *repository =
        engine::require(solo_param_repository, solo_param_repository_name.address());
    const ParamSlot &slot = repository->slots[static_cast<int32_t>(type)];
    if (slot.loaded == 0 || slot.holder == nullptr) return nullptr;
    // The holder's file is not checked, and neither is the file's table.
    return slot.holder->file->table;
}

// The per-type accessors: the row with id `id` of the table of `type`, or null when the id is
// negative (the repository is not read then), the table is not loaded, or it has no such row.
inline void find_row(RowLookup *out, int32_t id, ParamType type)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(type))
            row = find_row_in_table(table, static_cast<uint32_t>(id));
    }
    out->id = id;
    out->row = row;
}

inline void find_row_with_version(VersionedRowLookup *out, int32_t id, ParamType type)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(type)) {
            out->data_version = static_cast<uint8_t>(table->data_version);
            row = find_row_in_table(table, static_cast<uint32_t>(id));
        }
    }
    out->lookup.id = id;
    out->lookup.row = row;
}

}  // namespace param
