// SPDX-License-Identifier: GPL-2.0-or-later
// The per-type row lookups of the solo parameter repository: find the row with an id in one
// table type's PARAM file (param_repository.h). The game has one compiled copy of the same lookup
// per table type (two for GemGenParam); callers throughout the game read item, effect, face and
// menu data through them, from boot and the title menus on.
#include "param_repository.h"

using param::ParamType;
using param::RowLookup;

namespace {

// The ActionButtonParam and ReturnPointParam lookups fill a result that starts 8 bytes into the
// object they are given.
struct EmbeddedRowLookup {
    uint8_t unknown_0x00[0x8];
    RowLookup lookup;
};
static_assert(offsetof(EmbeddedRowLookup, lookup) == 0x8);

}  // namespace

extern "C" void bb_param_find_row_equip_param_accessory(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::equip_param_accessory);
}

extern "C" void bb_param_find_row_ai_sound(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::ai_sound);
}

extern "C" void bb_param_find_row_bullet(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::bullet);
}

extern "C" void bb_param_find_row_calc_correct_graph(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::calc_correct_graph);
}

extern "C" void bb_param_find_row_chara_init(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::chara_init);
}

extern "C" void bb_param_find_row_decal(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::decal);
}

extern "C" void bb_param_find_row_dungeon_sub_feat_lot(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::dungeon_sub_feat_lot);
}

extern "C" void bb_param_find_row_face(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::face);
}

extern "C" void bb_param_find_row_face_range(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::face_range);
}

extern "C" void bb_param_find_row_gem_category(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::gem_category);
}

extern "C" void bb_param_find_row_gem_drop_doping(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::gem_drop_doping);
}

extern "C" void bb_param_find_row_gem_drop_modify(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::gem_drop_modify);
}

extern "C" void bb_param_find_row_gem_effect(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::gem_effect);
}

extern "C" void bb_param_find_row_gem_gen(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::gem_gen);
}

extern "C" void bb_param_find_row_gem_gen_2(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::gem_gen);
}

extern "C" void bb_param_find_row_equip_param_goods(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::equip_param_goods);
}

extern "C" void bb_param_find_row_hit_mtrl(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::hit_mtrl);
}

extern "C" void bb_param_find_row_holygrail_ex(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::holygrail_ex);
}

extern "C" void bb_param_find_row_knock_back(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::knock_back);
}

extern "C" void bb_param_find_row_magic(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::magic);
}

extern "C" void bb_param_find_row_menu_color_table(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::menu_color_table);
}

extern "C" void bb_param_find_row_move(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::move);
}

extern "C" void bb_param_find_row_equip_mtrl_set(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::equip_mtrl_set);
}

extern "C" void bb_param_find_row_npc_think(param::NpcThinkRowLookup *out, int32_t id, int32_t word_0,
                                             int32_t word_1)
{
    // Stored first and whatever the lookup finds, as the original does.
    out->passed_word_0 = word_0;
    out->passed_word_1 = word_1;
    param::find_row(&out->lookup, id, ParamType::npc_think);
}

extern "C" void bb_param_find_row_object(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::object);
}

extern "C" void bb_param_find_row_reinforce_param_protector(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::reinforce_param_protector);
}

extern "C" void bb_param_find_row_ragdoll(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::ragdoll);
}

extern "C" void bb_param_find_row_ritual_required_mat(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::ritual_required_mat);
}

extern "C" void bb_param_find_row_shop_lineup(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::shop_lineup);
}

extern "C" void bb_param_find_row_skeleton(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::skeleton);
}

extern "C" void bb_param_find_row_sp_effect(param::VersionedRowLookup *out, int32_t id)
{
    param::find_row_with_version(out, id, ParamType::sp_effect);
}

extern "C" void bb_param_find_row_obj_act(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::obj_act);
}

extern "C" void bb_param_find_row_talk(param::VersionedRowLookup *out, int32_t id)
{
    param::find_row_with_version(out, id, ParamType::talk);
}

extern "C" void bb_param_find_row_weapon_gen(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::weapon_gen);
}

extern "C" void bb_param_find_row_reinforce_param_weapon(RowLookup *out, int32_t id)
{
    param::find_row(out, id, ParamType::reinforce_param_weapon);
}

extern "C" void bb_param_find_row_action_button(EmbeddedRowLookup *out, int32_t id)
{
    param::find_row(&out->lookup, id, ParamType::action_button);
}

extern "C" void bb_param_find_row_return_point(EmbeddedRowLookup *out, int32_t id)
{
    param::find_row(&out->lookup, id, ParamType::return_point);
}
