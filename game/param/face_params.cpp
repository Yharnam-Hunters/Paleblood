// SPDX-License-Identifier: GPL-2.0-or-later
// FaceGenParam and FaceParam, the two face tables of the solo parameter repository
// (param_repository.h), as character creation uses them. FaceGenParam rows hold a face as the
// creation screen builds it (geometry and texture sliders, and eight parts, each a part id with
// a color); FaceParam rows hold the preset faces, with body proportions and asymmetry. The game
// edits these rows in place: the setters below write one field of the row with an id, and
// apply_char_make_face writes a whole face from the creation screen into a FaceGenParam row.
//
// The field names are the community's for these tables (in comments where they differ).
//
// None of these ran in the coverage boots (they need the character creation screen).
//
// The setters do nothing for a row that is not there. The originals assume the row exists: most
// of them search the index without a not-found exit (a missing id never returns), and the rest
// write through a null row. Both are hangs or crashes in the game, so no case covers them.
#include <cstring>

#include "param_repository.h"

namespace {

using param::loaded_table;
using param::ParamType;
using param::TableHeader;

constexpr size_t face_geometry_count = 50, face_texture_count = 50;

enum ColorChannel : int32_t { red = 0, green = 1, blue = 2 };
constexpr int32_t color_channel_count = 3;

// One part of a FaceGenParam face: which model part, and its color.
struct FacePart {
    uint8_t parts_id;                    // xxxPartsId
    uint8_t color[color_channel_count];  // xxxColor_R, _G, _B
};
static_assert(sizeof(FacePart) == 4);

// The parts, in row order: face, hair, left eye, right eye, eyebrow, beard, accessories, decal.
enum FacePartIndex : int32_t { face_part = 0, hair_part = 1, left_eye_part = 2 };
constexpr int32_t face_part_count = 8;

// Where the decal sits on the face (decalPosX, decalPosY, decalAngle, decalScale).
struct DecalPlacement {
    uint8_t x, y, angle, scale;
};
static_assert(sizeof(DecalPlacement) == 4);

struct FaceGenRow {
    uint8_t geometry[face_geometry_count];  // faceGeoData00..49
    uint8_t texture[face_texture_count];    // faceTexData00..49
    FacePart parts[face_part_count];
    DecalPlacement decal;
};
static_assert(offsetof(FaceGenRow, texture) == 0x32 && offsetof(FaceGenRow, parts) == 0x64);
static_assert(offsetof(FaceGenRow, decal) == 0x84 && sizeof(FaceGenRow) == 0x88);

constexpr size_t preset_geometry_count = 61, preset_texture_count = 36, preset_asymmetry_count = 26;
constexpr int32_t body_scale_count = 5;  // head, breast, abdomen, arm, leg

struct FaceRow {
    uint8_t unknown_0x00[0x24];  // the parts and colors, as in FaceGenParam
    DecalPlacement decal;
    uint8_t body_scale[body_scale_count];  // chrBodyScaleHead, Breast, Abdomen, Arm, Leg
    uint8_t age;
    uint8_t gender;
    uint8_t caricature_geometry;
    uint8_t caricature_texture;
    uint8_t geometry[preset_geometry_count];   // faceGeoData00..60
    uint8_t texture[preset_texture_count];     // faceTexData00..35
    uint8_t asymmetry[preset_asymmetry_count];  // faceGeoAsymData00..25, used from data version 4
};
static_assert(offsetof(FaceRow, decal) == 0x24 && offsetof(FaceRow, body_scale) == 0x28);
static_assert(offsetof(FaceRow, age) == 0x2d && offsetof(FaceRow, caricature_texture) == 0x30);
static_assert(offsetof(FaceRow, geometry) == 0x31 && offsetof(FaceRow, texture) == 0x6e);
static_assert(offsetof(FaceRow, asymmetry) == 0x92 && sizeof(FaceRow) == 0xac);

// The table version that has the asymmetry fields (and, in FaceGenParam, the parts).
constexpr uint16_t face_tables_current_version = 4;

bool table_has_current_version(ParamType type)
{
    const TableHeader *table = loaded_table(type);
    return table != nullptr && table->data_version == face_tables_current_version;
}

// The row with `id` as a FaceGenParam or FaceParam row, for writing: character creation edits
// the loaded table in place. Null for a negative id, a table that is not loaded or a missing row.
template <typename Row> Row *editable_row(ParamType type, int32_t id)
{
    if (id < 0) return nullptr;
    const TableHeader *table = loaded_table(type);
    if (table == nullptr) return nullptr;
    const uint8_t *row = param::find_row_in_table(table, static_cast<uint32_t>(id));
    return reinterpret_cast<Row *>(const_cast<uint8_t *>(row));
}

// As editable_row, for setters that take an index: an index above `last` writes nothing. The
// index is checked after the table is fetched and before the search, as in the originals.
template <typename Row> Row *editable_row_for_index(ParamType type, int32_t id, uint32_t index, uint32_t last)
{
    if (id < 0) return nullptr;
    const TableHeader *table = loaded_table(type);
    if (table == nullptr || index > last) return nullptr;
    const uint8_t *row = param::find_row_in_table(table, static_cast<uint32_t>(id));
    return reinterpret_cast<Row *>(const_cast<uint8_t *>(row));
}

// One byte at a time, each read just before it is written, as the originals copy.
void copy_decal(DecalPlacement *to, const DecalPlacement *from)
{
    to->x = from->x;
    to->y = from->y;
    to->angle = from->angle;
    to->scale = from->scale;
}

FaceGenRow *face_gen_row(int32_t id) { return editable_row<FaceGenRow>(ParamType::face_gen, id); }
FaceRow *face_row(int32_t id) { return editable_row<FaceRow>(ParamType::face, id); }

// A FaceGenParam row and a copy of its sliders, as the face lookup returns them.
struct FaceGenRowCopy {
    int32_t id;
    uint8_t unknown_0x04[0x4];
    const uint8_t *row;
    uint8_t geometry[face_geometry_count];
    uint8_t texture[face_texture_count];
};
static_assert(offsetof(FaceGenRowCopy, geometry) == 0x10 && offsetof(FaceGenRowCopy, texture) == 0x42);

// The face the character creation screen is building.
struct CharMakeFace {
    uint8_t unknown_0x00[0x18];
    int32_t state;  // 1 while the face is being edited
    uint8_t unknown_0x1c[0x4];
    struct {
        uint8_t parts_id;
        uint8_t unknown_0x01[0x3];
    } part_ids[face_part_count + 1];
    struct {
        uint8_t color[color_channel_count];
        uint8_t unknown;
    } part_colors[face_part_count + 1];
    DecalPlacement decal;
    uint8_t unknown_0x6c[0x34];
    uint8_t geometry[face_geometry_count];
    uint8_t texture[face_texture_count];
};
static_assert(offsetof(CharMakeFace, state) == 0x18 && offsetof(CharMakeFace, part_ids) == 0x20);
static_assert(offsetof(CharMakeFace, part_colors) == 0x44 && offsetof(CharMakeFace, decal) == 0x68);
static_assert(offsetof(CharMakeFace, geometry) == 0xa0 && offsetof(CharMakeFace, texture) == 0xd2);
constexpr int32_t face_being_edited = 1;
// The slider value written instead of the face's own when it is no longer being edited.
constexpr uint8_t neutral_slider = 0x80;

}  // namespace

// Whether the FaceGenParam / FaceParam table is loaded and of the current version (4).
extern "C" bool bb_param_face_gen_is_current(void)
{
    return table_has_current_version(ParamType::face_gen);
}

extern "C" bool bb_param_face_is_current(void)
{
    return table_has_current_version(ParamType::face);
}

// The FaceGenParam row with `id` and a copy of its geometry and texture sliders. Without a row
// (also for a negative id, or a table not loaded) the sliders are zero.
extern "C" void bb_param_find_row_face_gen(FaceGenRowCopy *out, int32_t id)
{
    const uint8_t *row = nullptr;
    if (id >= 0) {
        if (const TableHeader *table = loaded_table(ParamType::face_gen))
            row = param::find_row_in_table(table, static_cast<uint32_t>(id));
    }
    out->id = id;
    out->row = row;
    if (row == nullptr) {
        std::memset(out->geometry, 0, sizeof(out->geometry));
        std::memset(out->texture, 0, sizeof(out->texture));
        return;
    }
    // Byte by byte, so an overlapping row and result copy as the original's loops do. The row
    // pointer is read back from the result for the second copy, which the first may have changed.
    const auto *sliders = reinterpret_cast<const FaceGenRow *>(row);
    for (size_t i = 0; i < face_geometry_count; ++i) out->geometry[i] = sliders->geometry[i];
    sliders = reinterpret_cast<const FaceGenRow *>(out->row);
    for (size_t i = 0; i < face_texture_count; ++i) out->texture[i] = sliders->texture[i];
}

extern "C" void bb_param_face_gen_set_geometry(int32_t id, uint32_t index, uint8_t value)
{
    if (FaceGenRow *row = editable_row_for_index<FaceGenRow>(ParamType::face_gen, id, index, face_geometry_count - 1))
        row->geometry[index] = value;
}

extern "C" void bb_param_face_gen_set_texture(int32_t id, uint32_t index, uint8_t value)
{
    if (FaceGenRow *row = editable_row_for_index<FaceGenRow>(ParamType::face_gen, id, index, face_texture_count - 1))
        row->texture[index] = value;
}

extern "C" void bb_param_face_gen_set_part(int32_t id, uint32_t part, uint8_t parts_id)
{
    if (FaceGenRow *row = editable_row_for_index<FaceGenRow>(ParamType::face_gen, id, part, face_part_count - 1))
        row->parts[part].parts_id = parts_id;
}

// The channel is checked first, before the repository is read; the part after the table.
extern "C" void bb_param_face_gen_set_part_color(int32_t id, uint32_t part, uint32_t channel, uint8_t value)
{
    if (id < 0 || channel >= color_channel_count) return;
    if (FaceGenRow *row = editable_row_for_index<FaceGenRow>(ParamType::face_gen, id, part, face_part_count - 1))
        row->parts[part].color[channel] = value;
}

// Copies the four decal placement bytes from `decal` (nothing for a null pointer).
extern "C" void bb_param_face_gen_set_decal(int32_t id, const DecalPlacement *decal)
{
    if (id < 0 || decal == nullptr) return;
    if (FaceGenRow *row = face_gen_row(id)) copy_decal(&row->decal, decal);
}

extern "C" void bb_param_face_gen_set_hair_parts_id(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[hair_part].parts_id = value;
}

extern "C" void bb_param_face_gen_set_hair_red(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[hair_part].color[red] = value;
}

extern "C" void bb_param_face_gen_set_hair_green(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[hair_part].color[green] = value;
}

extern "C" void bb_param_face_gen_set_hair_blue(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[hair_part].color[blue] = value;
}

extern "C" void bb_param_face_gen_set_left_eye_red(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[left_eye_part].color[red] = value;
}

extern "C" void bb_param_face_gen_set_left_eye_green(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[left_eye_part].color[green] = value;
}

extern "C" void bb_param_face_gen_set_left_eye_blue(int32_t id, uint8_t value)
{
    if (FaceGenRow *row = face_gen_row(id)) row->parts[left_eye_part].color[blue] = value;
}

// Writes the face being built on the character creation screen into FaceGenParam row `id`:
// its sliders, then (for a version 4 table) its nine part ids and colors and the decal
// placement. The ninth part has no field in the row; the setters ignore it.
extern "C" void bb_param_apply_char_make_face(const CharMakeFace *face, int32_t id)
{
    if (face->state != face_being_edited) return;
    FaceGenRowCopy current;
    current.id = -1;
    current.row = nullptr;
    bb_param_find_row_face_gen(&current, id);
    if (current.row == nullptr) return;
    // The state is read again for every value: a write to the row may change the face (the
    // original does not assume they are apart).
    for (uint32_t i = 0; i < face_geometry_count; ++i)
        bb_param_face_gen_set_geometry(id, i, face->state == face_being_edited ? face->geometry[i] : neutral_slider);
    for (uint32_t i = 0; i < face_texture_count; ++i)
        bb_param_face_gen_set_texture(id, i, face->state == face_being_edited ? face->texture[i] : neutral_slider);
    if (!table_has_current_version(ParamType::face_gen)) return;
    for (uint32_t part = 0; part <= face_part_count; ++part) {
        bb_param_face_gen_set_part(id, part, face->part_ids[part].parts_id);
        for (uint32_t channel = 0; channel < color_channel_count; ++channel)
            bb_param_face_gen_set_part_color(id, part, channel, face->part_colors[part].color[channel]);
    }
    bb_param_face_gen_set_decal(id, &face->decal);
}

// Copies the four decal placement bytes of a FaceParam preset (nothing for a null pointer).
extern "C" void bb_param_face_set_decal(int32_t id, const DecalPlacement *decal)
{
    if (id < 0 || decal == nullptr) return;
    if (FaceRow *row = face_row(id)) copy_decal(&row->decal, decal);
}

// The index is checked after the search here.
extern "C" void bb_param_face_set_body_scale(int32_t id, uint32_t index, uint8_t value)
{
    FaceRow *row = face_row(id);
    if (row != nullptr && index < body_scale_count) row->body_scale[index] = value;
}

extern "C" void bb_param_face_set_age(int32_t id, uint8_t value)
{
    if (FaceRow *row = face_row(id)) row->age = value;
}

extern "C" void bb_param_face_set_gender(int32_t id, uint8_t value)
{
    if (FaceRow *row = face_row(id)) row->gender = value;
}

extern "C" void bb_param_face_set_caricature_geometry(int32_t id, uint8_t value)
{
    if (FaceRow *row = face_row(id)) row->caricature_geometry = value;
}

extern "C" void bb_param_face_set_caricature_texture(int32_t id, uint8_t value)
{
    if (FaceRow *row = face_row(id)) row->caricature_texture = value;
}

extern "C" void bb_param_face_set_geometry(int32_t id, uint32_t index, uint8_t value)
{
    if (FaceRow *row = editable_row_for_index<FaceRow>(ParamType::face, id, index, preset_geometry_count - 1))
        row->geometry[index] = value;
}

extern "C" void bb_param_face_set_texture(int32_t id, uint32_t index, uint8_t value)
{
    if (FaceRow *row = editable_row_for_index<FaceRow>(ParamType::face, id, index, preset_texture_count - 1))
        row->texture[index] = value;
}

// Only a version 4 table has the asymmetry fields: an older one is left alone.
extern "C" void bb_param_face_set_asymmetry(int32_t id, uint32_t index, uint8_t value)
{
    if (id < 0) return;
    const TableHeader *table = loaded_table(ParamType::face);
    if (table == nullptr || index >= preset_asymmetry_count) return;
    if (table->data_version != face_tables_current_version) return;
    if (const uint8_t *row = param::find_row_in_table(table, static_cast<uint32_t>(id)))
        reinterpret_cast<FaceRow *>(const_cast<uint8_t *>(row))->asymmetry[index] = value;
}
