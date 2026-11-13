<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Parameters

Most of Bloodborne's numbers are not in its code. Weapon damage and scaling, armor, items, the effects of consumables and blood gems, enemy stats and AI tuning, item drops, shop stock, the lamps you warp between, and the colors and layout of menus all live in parameter tables: PARAM files the game loads. The code asks a table for a row by its id and reads fields from that row. This page covers the part of the game that holds those tables and answers those questions. It does not describe what the tables contain, which is game data.

**At a glance**

- **Scope:** Paleblood's `game/param/`: the solo parameter repository, the per-table row lookups, the equipment, menu, map and return-point lookups, row names and rows by position.
- **Verified:** Every replaced lookup is edge-verified against the original on invented tables. None is `verified` yet: none has been checked on frames recorded from a real game run.
- **Open:** The loader that reads the files and builds their search index; most header words; why the row-name lookups exist in the retail build.

## Summary

### The solo parameter repository

All the tables a single-player game needs are kept in one object, the **SoloParamRepository**,
a singleton (`0x05940340`). It has one slot per table type, 62 types in all. A slot records
whether its table is loaded, and points through a resource holder to the loaded file.

Every lookup first asks the repository for its table. A missing repository is reported with the
engine's singleton check (see [Engine](engine.md)) and then read anyway, as the original does.
A slot that is not loaded gives "no row".

The 62 types, in slot order (the names are the modding community's names for these tables):

| Slot | Table | Slot | Table | Slot | Table |
|---:|---|---:|---|---:|---|
| 0 | EquipParamWeapon | 21 | CharaInitParam | 42 | GemGenParam |
| 1 | EquipParamProtector | 22 | EquipMtrlSetParam | 43 | GemeffectParam |
| 2 | EquipParamAccessory | 23 | FaceGenParam | 44 | GemCategoryParam |
| 3 | EquipParamGoods | 24 | FaceParam | 45 | GemDropDopingParam |
| 4 | ReinforceParamWeapon | 25 | FaceRangeParam | 46 | GemDropModifyParam |
| 5 | ReinforceParamProtector | 26 | RagdollParam | 47 | ResidentFxParam |
| 6 | NpcParam | 27 | ShopLineupParam | 48 | AiSoundParam |
| 7 | AtkParam_Npc | 28 | QwcChange | 49 | GameMapParam |
| 8 | AtkParam_Pc | 29 | QwcJudge | 50 | ReturnPointParam |
| 9 | NpcThinkParam | 30 | GameAreaParam | 51 | ItemLotParam_Lvdep |
| 10 | ObjectParam | 31 | SkeletonParam | 52 | HolygrailExParam |
| 11 | Bullet | 32 | CalcCorrectGraph | 53 | DungeonFeatureParam |
| 12 | BehaviorParam | 33 | LockCamParam | 54 | DungeonSubFeatLotParam |
| 13 | BehaviorParam_PC | 34 | ObjActParam | 55 | RitualRequiredMatParam |
| 14 | Magic | 35 | HitMtrlParam | 56 | CharMakeMenuTopParam |
| 15 | SpEffectParam | 36 | KnockBackParam | 57 | CharMakeMenuListItemParam |
| 16 | SpEffectVfxParam | 37 | Wind | 58 | NewMenuColorTableParam |
| 17 | TalkParam | 38 | DecalParam | 59 | MenuPropertySpecParam |
| 18 | MenuColorTableParam | 39 | ActionButtonParam | 60 | MenuPropertyLayoutParam |
| 19 | ItemLotParam | 40 | WeaponGenParam | 61 | MenuValueTableParam |
| 20 | MoveParam | 41 | ProtectorGenParam | | |

Chalice dungeons have their own tables (HolygrailEx, DungeonFeature, DungeonSubFeatLot,
RitualRequiredMat, and the Gem tables for blood gems), and so does character creation
(CharMakeMenuTop and CharMakeMenuListItem).

### Inside a PARAM file

A PARAM file is a header, a list of **row descriptors**, and then the rows' data and names.
Bloodborne's files follow the layout the modding community documents for FromSoftware's games:

- The header holds the row count (16 bits), a data version (16 bits), and a format byte with a
  flags byte after it.
- Format 2 files have a 48-byte header followed by 12-byte descriptors (row id, data offset,
  name offset). Later formats have a 64-byte header, and from format 4 on a flag switches to
  24-byte descriptors with 64-bit offsets.
- Offsets count from the start of the header. A name offset of 0 means the row has no name.

### The search index

The game does not search the descriptors directly. Each loaded file is followed by a **search
index**: (row id, row number) pairs sorted by id, placed at the file's size rounded up to a
multiple of 16. The size is the word at `-0x10` from the header. Every lookup by id is a binary
search of this index, with ids compared as unsigned numbers. The code that loads the files and
builds the index is not replaced yet.

### How the game asks for rows

The game has a separate compiled copy of each lookup for each table type that needs it. They are
all built from a few templates, and Paleblood's replacements share one implementation of each.

- **Row by id.** It returns the id asked for and the row, or no row. A negative id returns at
  once, without touching the repository. Most types use this plain form
  ([#687](https://github.com/Yharnam-Hunters/Paleblood/pull/687)); GemGenParam has two identical
  copies. A few copies add something:
    - SpEffectParam and TalkParam also return the table's data version.
    - NpcThinkParam stores two extra words beside the result, unread.
    - ActionButtonParam and ReturnPointParam fill a result that starts `+0x8` into the object they
      are given ([#697](https://github.com/Yharnam-Hunters/Paleblood/pull/697)).
    - ItemLotParam gives no row unless the table's data version is 2 or later (#697).
    - DungeonFeatureParam copies a header word (`+0x06`, meaning unknown) to a global on every
      search, found or not (#697).
    - NpcParam also returns that header word and the data version, and reports a table of version
      2 or older through the engine's error report, then searches it anyway
      ([#714](https://github.com/Yharnam-Hunters/Paleblood/pull/714)).
- **Equipment** (#697). The last two decimal digits of a weapon or armor id are its reinforcement
  level. The lookup finds the base item's row (the id rounded down to a multiple of 100), then the
  ReinforceParam row for the item's reinforcement type plus the level. Without a base row it still
  asks the reinforcement table, for id -1.
- **Runs of rows** (#697). Character creation's list items and some menu tables are read as
  consecutive ids: the lookup returns the first row and how many rows follow it without a gap. The
  NewMenuColorTableParam run falls back to 16 rows built into the executable when the table has none.
- **A built-in fallback** (#697). The menu property lookup uses the table's row when there is one,
  otherwise the built-in row with that id, otherwise the first of 13 rows compiled into the
  executable.
- **Row names** (#714). A row can carry a name (an editor's label, kept as text after the rows).
  Ten table types have a lookup that returns it, or nothing; GemGenParam again has two copies.
  Unlike the data lookups, this one checks the index's row number against the row count.
- **Row by position** (#714). CharaInitParam, EquipParamGoods, Magic and MenuColorTableParam can be
  read in file order: row *n* with its id, or id -1 when the table is shorter.
- **Maps and lamps** (#714).
    - GameMapParam, a map's lighting set-up, is found by the map's number. A map named
      `mAA_BB_CC_DD` is row `AABBCCDD`, and maps without their own row use row 0.
    - ReturnPointParam lists the return points: the lamps players warp between and revive at. It
      is searched row by row, in file order: the warp-menu slot of a lamp, the lamp in a warp-menu
      slot, or every lamp with a warp-menu slot in one map area and block.

<details>
<summary>How the lookups were verified</summary>

Each replacement is readable C++ in `game/param/` and passes `verify.py` against the original
on invented tables built by the fixtures beside it (synthetic cases, never game files). They
are recorded as edge-verified in [#687](https://github.com/Yharnam-Hunters/Paleblood/pull/687),
[#697](https://github.com/Yharnam-Hunters/Paleblood/pull/697) and
[#714](https://github.com/Yharnam-Hunters/Paleblood/pull/714). None has been checked on frames
recorded from a real game run, so none is `verified`. The row-name lookups did not run in the
coverage boots #714 describes.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x018e4110` | 432 | `param_apply_char_make_face` | edge-verified | Character creation: writes the face being built (sliders; parts/colors/decal for a version 4 table) into a FaceGenParam row |
| `0x01f82cc0` | 457 | `param_find_rows_new_menu_color_table` | edge-verified | NewMenuColorTableParam run of consecutive ids from an id: first row (a built-in default row when none) and count |
| `0x01f83130` | 457 | `param_find_rows_char_make_menu_list_item` | edge-verified | Character creation menu list items: first CharMakeMenuListItemParam row of an id and the count of rows with consecutive ids after it |
| `0x0204a2c0` | 155 | `param_table_view` | edge-verified | SoloParamRepository fills a table view object (vtable 0x05749140 and the table of a type or null); returns it |
| `0x0206ddd0` | 547 | `param_find_rows_menu_value_table` | edge-verified | MenuValueTableParam run of consecutive ids from an id: first row and count; fetches the table again for every id |
| `0x0206ecc0` | 313 | `param_menu_property_spec` | edge-verified | MenuPropertySpecParam row of an id; else the built-in row (13 at 0x05528730) with that id; else the first built-in row |
| `0x02311c80` | 283 | `param_find_row_equip_param_accessory` | edge-verified | SoloParamRepository row lookup for EquipParamAccessory (type 2): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02311da0` | 284 | `param_row_name_equip_param_accessory` | edge-verified | SoloParamRepository row name lookup for EquipParamAccessory (type 2): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x02312070` | 309 | `param_next_row_id_equip_param_accessory` | edge-verified | SoloParamRepository id of the row after id in EquipParamAccessory (type 2) file order; negative id: first row; -1 when none |
| `0x023121b0` | 341 | `param_previous_row_id_equip_param_accessory` | edge-verified | SoloParamRepository id of the row before id in EquipParamAccessory (type 2) file order; negative id: last row; -1 when none |
| `0x02312370` | 402 | `param_list_listed_ids_equip_param_accessory` | edge-verified | SoloParamRepository as list_row_ids_equip_param_accessory but skips rows whose int16 at +0x26 is negative |
| `0x02312520` | 390 | `param_list_row_ids_equip_param_accessory` | edge-verified | SoloParamRepository empties an id array and appends every EquipParamAccessory (type 2) row id in file order; returns the count |
| `0x023128a0` | 284 | `param_find_row_action_button` | edge-verified | SoloParamRepository row lookup for ActionButtonParam (type 39): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02312b60` | 283 | `param_find_row_ai_sound` | edge-verified | SoloParamRepository row lookup for AiSoundParam (type 48): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02312fd0` | 310 | `param_find_row_atk_param` | edge-verified | SoloParamRepository AtkParam row lookup (user 0: AtkParam_Npc type 7; 1: AtkParam_Pc type 8; args out/user/id); stores the user beside the result |
| `0x023133c0` | 310 | `param_find_row_behavior_param` | edge-verified | SoloParamRepository BehaviorParam row lookup (user 0: BehaviorParam type 12; 1: BehaviorParam_PC type 13; args out/id/user; a negative user gives no row) |
| `0x02314860` | 283 | `param_find_row_bullet` | edge-verified | SoloParamRepository row lookup for Bullet (type 11): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02314b60` | 283 | `param_find_row_calc_correct_graph` | edge-verified | SoloParamRepository row lookup for CalcCorrectGraph (type 32): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02314e20` | 283 | `param_find_row_chara_init` | edge-verified | SoloParamRepository row lookup for CharaInitParam (type 21): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02314f40` | 278 | `param_row_at_chara_init` | edge-verified | SoloParamRepository row-at accessor for CharaInitParam (type 21): id and data of row number n (id -1; null past the count or when not loaded) |
| `0x02315060` | 96 | `param_row_count_chara_init` | edge-verified | SoloParamRepository row count of CharaInitParam (type 21); 0 when not loaded |
| `0x023150d0` | 284 | `param_row_name_chara_init` | edge-verified | SoloParamRepository row name lookup for CharaInitParam (type 21): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x023155c0` | 283 | `param_find_row_decal` | edge-verified | SoloParamRepository row lookup for DecalParam (type 38): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x023167f0` | 288 | `param_find_row_dungeon_feature` | edge-verified | DungeonFeatureParam row lookup (type 53): as the other row lookups; each search also stores the header word 0x06 at 0x05992cb0 |
| `0x02316c70` | 283 | `param_find_row_dungeon_sub_feat_lot` | edge-verified | SoloParamRepository row lookup for DungeonSubFeatLotParam (type 54): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02317560` | 547 | `param_find_row_face_gen` | edge-verified | FaceGenParam row of an id plus a copy of its 50 geometry and 50 texture sliders (zeros without a row) |
| `0x02317790` | 103 | `param_face_gen_is_current` | edge-verified | Whether FaceGenParam is loaded and of data version 4 |
| `0x02317800` | 284 | `param_row_name_face_gen` | edge-verified | SoloParamRepository row name lookup for FaceGenParam (type 23): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x02317ad0` | 260 | `param_face_gen_set_geometry` | edge-verified | Character creation: sets FaceGenParam faceGeoData[index] (index <= 49) of a row |
| `0x02317be0` | 261 | `param_face_gen_set_texture` | edge-verified | Character creation: sets FaceGenParam faceTexData[index] (index <= 49) of a row |
| `0x02317cf0` | 1384 | `param_face_gen_set_part` | edge-verified | Character creation: sets the parts id of FaceGenParam part 0..7 (face/hair/eyes/eyebrow/beard/accessories/decal) |
| `0x02318280` | 1543 | `param_face_gen_set_part_color` | edge-verified | Character creation: sets color channel 0..2 of FaceGenParam part 0..7 |
| `0x023188b0` | 317 | `param_face_gen_set_decal` | edge-verified | Character creation: copies the 4 decal placement bytes into a FaceGenParam row |
| `0x023189f0` | 242 | `param_face_gen_set_hair_parts_id` | edge-verified | Sets FaceGenParam hairPartsId of a row |
| `0x02318af0` | 242 | `param_face_gen_set_hair_red` | edge-verified | Sets FaceGenParam hairColor_R of a row |
| `0x02318bf0` | 242 | `param_face_gen_set_hair_green` | edge-verified | Sets FaceGenParam hairColor_G of a row |
| `0x02318cf0` | 242 | `param_face_gen_set_hair_blue` | edge-verified | Sets FaceGenParam hairColor_B of a row |
| `0x02318df0` | 242 | `param_face_gen_set_left_eye_red` | edge-verified | Sets FaceGenParam eyeLColor_R of a row |
| `0x02318ef0` | 242 | `param_face_gen_set_left_eye_green` | edge-verified | Sets FaceGenParam eyeLColor_G of a row |
| `0x02318ff0` | 242 | `param_face_gen_set_left_eye_blue` | edge-verified | Sets FaceGenParam eyeLColor_B of a row |
| `0x023192d0` | 283 | `param_find_row_face` | edge-verified | SoloParamRepository row lookup for FaceParam (type 24): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x023193f0` | 103 | `param_face_is_current` | edge-verified | Whether FaceParam is loaded and of data version 4 |
| `0x0231a340` | 305 | `param_face_set_decal` | edge-verified | Copies the 4 decal placement bytes into a FaceParam row |
| `0x0231a480` | 320 | `param_face_set_body_scale` | edge-verified | Sets FaceParam body scale[index] (head/breast/abdomen/arm/leg) |
| `0x0231a5e0` | 242 | `param_face_set_age` | edge-verified | Sets FaceParam age of a row |
| `0x0231a6e0` | 242 | `param_face_set_gender` | edge-verified | Sets FaceParam gender of a row |
| `0x0231a7e0` | 242 | `param_face_set_caricature_geometry` | edge-verified | Sets FaceParam caricatureGeometry of a row |
| `0x0231a8e0` | 242 | `param_face_set_caricature_texture` | edge-verified | Sets FaceParam caricatureTexture of a row |
| `0x0231a9e0` | 261 | `param_face_set_geometry` | edge-verified | Sets FaceParam faceGeoData[index] (index <= 60) |
| `0x0231aaf0` | 261 | `param_face_set_texture` | edge-verified | Sets FaceParam faceTexData[index] (index <= 35) |
| `0x0231ac00` | 280 | `param_face_set_asymmetry` | edge-verified | Sets FaceParam faceGeoAsymData[index] (index <= 25) when the table is version 4 |
| `0x0231af40` | 283 | `param_find_row_face_range` | edge-verified | SoloParamRepository row lookup for FaceRangeParam (type 25): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231be50` | 443 | `param_find_row_game_map` | edge-verified | GameMapParam row of a packed map id mAA_BB_CC_DD (row id AABBCCDD); falls back to row 0; id -1 for no map |
| `0x0231c1e0` | 283 | `param_find_row_gem_category` | edge-verified | SoloParamRepository row lookup for GemCategoryParam (type 44): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231c380` | 366 | `param_list_gem_category_by_selector_0c` | edge-verified | SoloParamRepository appends the GemCategoryParam (type 44) row lookup of every row whose int32 at +0x0c matches |
| `0x0231c510` | 366 | `param_list_gem_category_by_selector_08` | edge-verified | SoloParamRepository appends the GemCategoryParam (type 44) row lookup of every row whose int32 at +0x08 matches |
| `0x0231ca40` | 283 | `param_find_row_gem_drop_doping` | edge-verified | SoloParamRepository row lookup for GemDropDopingParam (type 45): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231cd30` | 283 | `param_find_row_gem_drop_modify` | edge-verified | SoloParamRepository row lookup for GemDropModifyParam (type 46): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231d0f0` | 283 | `param_find_row_gem_effect` | edge-verified | SoloParamRepository row lookup for GemeffectParam (type 43): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231d210` | 284 | `param_row_name_gem_effect` | edge-verified | SoloParamRepository row name lookup for GemeffectParam (type 43): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x0231d330` | 329 | `param_gem_effect_for_level` | edge-verified | SoloParamRepository GemeffectParam (type 43): id of the row of a group whose level range holds the level with the highest rank up to the level; -1 when none |
| `0x0231d630` | 283 | `param_find_row_gem_gen` | edge-verified | SoloParamRepository row lookup for GemGenParam (type 42): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231d750` | 284 | `param_row_name_gem_gen` | edge-verified | SoloParamRepository row name lookup for GemGenParam (type 42): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x0231da10` | 209 | `param_gem_gen_first_row_from` | edge-verified | SoloParamRepository GemGenParam (type 42): number of the first row whose id is at least id; -1 when none |
| `0x0231daf0` | 213 | `param_gem_gen_last_row_to` | edge-verified | SoloParamRepository GemGenParam (type 42): number of the last row whose id is at most id; -1 when none |
| `0x0231dbe0` | 184 | `param_gem_gen_row_id` | edge-verified | SoloParamRepository GemGenParam (type 42): id of row number n (unchecked against the count); -1 for a negative n or no table |
| `0x0231de40` | 283 | `param_find_row_gem_gen_2` | edge-verified | SoloParamRepository row lookup for GemGenParam (type 42): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231df60` | 284 | `param_row_name_gem_gen_2` | edge-verified | SoloParamRepository row name lookup for GemGenParam (type 42): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x0231e3d0` | 283 | `param_find_row_equip_param_goods` | edge-verified | SoloParamRepository row lookup for EquipParamGoods (type 3): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231e4f0` | 278 | `param_row_at_equip_param_goods` | edge-verified | SoloParamRepository row-at accessor for EquipParamGoods (type 3): id and data of row number n (id -1; null past the count or when not loaded) |
| `0x0231e610` | 96 | `param_row_count_equip_param_goods` | edge-verified | SoloParamRepository row count of EquipParamGoods (type 3); 0 when not loaded |
| `0x0231e680` | 284 | `param_row_name_equip_param_goods` | edge-verified | SoloParamRepository row name lookup for EquipParamGoods (type 3): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x0231ebb0` | 309 | `param_next_row_id_equip_param_goods` | edge-verified | SoloParamRepository id of the row after id in EquipParamGoods (type 3) file order; negative id: first row; -1 when none |
| `0x0231ecf0` | 341 | `param_previous_row_id_equip_param_goods` | edge-verified | SoloParamRepository id of the row before id in EquipParamGoods (type 3) file order; negative id: last row; -1 when none |
| `0x0231f0e0` | 390 | `param_list_row_ids_equip_param_goods` | edge-verified | SoloParamRepository empties an id array and appends every EquipParamGoods (type 3) row id in file order; returns the count |
| `0x0231f460` | 283 | `param_find_row_hit_mtrl` | edge-verified | SoloParamRepository row lookup for HitMtrlParam (type 35): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231f720` | 283 | `param_find_row_holygrail_ex` | edge-verified | SoloParamRepository row lookup for HolygrailExParam (type 52): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x0231f840` | 284 | `param_row_name_holygrail_ex` | edge-verified | SoloParamRepository row name lookup for HolygrailExParam (type 52): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x0231ff00` | 96 | `param_row_count_holygrail_ex` | edge-verified | SoloParamRepository row count of HolygrailExParam (type 52); 0 when not loaded |
| `0x0231ff70` | 168 | `param_holygrail_ex_row_number` | edge-verified | SoloParamRepository HolygrailExParam (type 52): index row number of a lookup id; -1 when none |
| `0x02320020` | 212 | `param_find_row_number_holygrail_ex` | edge-verified | SoloParamRepository HolygrailExParam (type 52): row number n looked up again by its id |
| `0x02320640` | 285 | `param_find_row_item_lot` | edge-verified | ItemLotParam row lookup (type 19): as the other row lookups; but only for tables of data version 2 or later |
| `0x02320dd0` | 283 | `param_find_row_knock_back` | edge-verified | SoloParamRepository row lookup for KnockBackParam (type 36): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x023210e0` | 283 | `param_find_row_magic` | edge-verified | SoloParamRepository row lookup for Magic (type 14): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02321200` | 278 | `param_row_at_magic` | edge-verified | SoloParamRepository row-at accessor for Magic (type 14): id and data of row number n (id -1; null past the count or when not loaded) |
| `0x02321320` | 96 | `param_row_count_magic` | edge-verified | SoloParamRepository row count of Magic (type 14); 0 when not loaded |
| `0x02321680` | 302 | `param_next_row_id_magic` | edge-verified | SoloParamRepository id of the row after id in Magic (type 14) file order; negative id: first row; -1 when none |
| `0x023217c0` | 341 | `param_previous_row_id_magic` | edge-verified | SoloParamRepository id of the row before id in Magic (type 14) file order; negative id: last row; -1 when none |
| `0x02321ac0` | 283 | `param_find_row_menu_color_table` | edge-verified | SoloParamRepository row lookup for MenuColorTableParam (type 18): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02321be0` | 278 | `param_row_at_menu_color_table` | edge-verified | SoloParamRepository row-at accessor for MenuColorTableParam (type 18): id and data of row number n (id -1; null past the count or when not loaded) |
| `0x02321ea0` | 283 | `param_find_row_move` | edge-verified | SoloParamRepository row lookup for MoveParam (type 20): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02322260` | 283 | `param_find_row_equip_mtrl_set` | edge-verified | SoloParamRepository row lookup for EquipMtrlSetParam (type 22): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x023226c0` | 343 | `param_find_row_npc` | edge-verified | SoloParamRepository row lookup for NpcParam (type 6): also returns header word 0x06 and the data version; reports versions 2 and older (fatal_error) and searches anyway |
| `0x02322b40` | 283 | `param_find_row_npc_think` | edge-verified | SoloParamRepository row lookup for NpcThinkParam (type 9): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02323290` | 283 | `param_find_row_object` | edge-verified | SoloParamRepository row lookup for ObjectParam (type 10): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x023235b0` | 231 | `param_clamp_protector_gen_indexes` | edge-verified | SoloParamRepository ProtectorGenParam (type 41) after loading: marks entry indexes above 63 as none (0x80000000) in place |
| `0x02323840` | 394 | `param_find_row_equip_param_protector` | edge-verified | EquipParamProtector lookup: row of the id rounded down to 100; then ReinforceParamProtector row for id%100 + reinforce type (s16 at +0xc8) |
| `0x023239d0` | 284 | `param_row_name_equip_param_protector` | edge-verified | SoloParamRepository row name lookup for EquipParamProtector (type 1): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x02324fa0` | 309 | `param_next_row_id_equip_param_protector` | edge-verified | SoloParamRepository id of the row after id in EquipParamProtector (type 1) file order; negative id: first row; -1 when none |
| `0x023250e0` | 341 | `param_previous_row_id_equip_param_protector` | edge-verified | SoloParamRepository id of the row before id in EquipParamProtector (type 1) file order; negative id: last row; -1 when none |
| `0x02325820` | 390 | `param_list_row_ids_equip_param_protector` | edge-verified | SoloParamRepository empties an id array and appends every EquipParamProtector (type 1) row id in file order; returns the count |
| `0x02326200` | 283 | `param_find_row_reinforce_param_protector` | edge-verified | SoloParamRepository row lookup for ReinforceParamProtector (type 5): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x023264c0` | 283 | `param_find_row_ragdoll` | edge-verified | SoloParamRepository row lookup for RagdollParam (type 26): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02326780` | 311 | `param_find_row_resident_fx` | edge-verified | SoloParamRepository ResidentFxParam (type 47) row lookup that also returns the index row number and the table |
| `0x02326b40` | 284 | `param_find_row_return_point` | edge-verified | SoloParamRepository row lookup for ReturnPointParam (type 50): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02326c60` | 238 | `param_return_point_warp_chair` | edge-verified | ReturnPointParam scan: warp chair number of the first row with an entity id; or -1 |
| `0x02326d60` | 241 | `param_return_point_entity` | edge-verified | ReturnPointParam scan: entity id of the first row with a warp chair number; or -1 |
| `0x02326e60` | 271 | `param_return_points_of_map` | edge-verified | ReturnPointParam scan: pushes the entity id of every row of an area/block with a warp chair onto an id array |
| `0x02327120` | 283 | `param_find_row_ritual_required_mat` | edge-verified | SoloParamRepository row lookup for RitualRequiredMatParam (type 55): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02327450` | 283 | `param_find_row_shop_lineup` | edge-verified | SoloParamRepository row lookup for ShopLineupParam (type 27): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02327880` | 283 | `param_find_row_skeleton` | edge-verified | SoloParamRepository row lookup for SkeletonParam (type 31): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02328aa0` | 283 | `param_find_row_sp_effect` | edge-verified | SoloParamRepository row lookup for SpEffectParam (type 15): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02328f90` | 283 | `param_find_row_obj_act` | edge-verified | SoloParamRepository row lookup for ObjActParam (type 34): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02329250` | 283 | `param_find_row_talk` | edge-verified | SoloParamRepository row lookup for TalkParam (type 17): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02329510` | 283 | `param_find_row_weapon_gen` | edge-verified | SoloParamRepository row lookup for WeaponGenParam (type 40): binary search of the loader index by id; writes id and row data pointer (null if none) |
| `0x02329630` | 284 | `param_row_name_weapon_gen` | edge-verified | SoloParamRepository row name lookup for WeaponGenParam (type 40): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x02329810` | 231 | `param_clamp_weapon_gen_indexes` | edge-verified | SoloParamRepository WeaponGenParam (type 40) after loading: marks entry indexes above 63 as none (0x80000000) in place |
| `0x02329aa0` | 378 | `param_find_row_equip_param_weapon` | edge-verified | EquipParamWeapon lookup: row of the id rounded down to 100; then ReinforceParamWeapon row for id%100 + reinforce type (s16 at +0xd6) |
| `0x02329c20` | 268 | `param_row_name_equip_param_weapon` | edge-verified | SoloParamRepository row name lookup for EquipParamWeapon (type 0): name of the row with the id a lookup result holds (index row number checked against the row count); null if none or unnamed |
| `0x0232c930` | 283 | `param_find_row_reinforce_param_weapon` | edge-verified | SoloParamRepository row lookup for ReinforceParamWeapon (type 4): binary search of the loader index by id; writes id and row data pointer (null if none) |

## Structs and globals

**SoloParamRepository** (`0x05940340`): the table slots start at `+0x70`, one per type.

| Offset | Field of a slot |
|---|---|
| `+0x0` | loaded flag (zero until the table is loaded) |
| `+0x8` | resource holder; its file is at holder `+0x70`, and the table header at file `+0x70` |

**PARAM header** (only the fields the lookups read)

| Offset | Field |
|---|---|
| `+0x06` | header word, meaning unknown (NpcParam and DungeonFeatureParam pass it on) |
| `+0x08` | data version |
| `+0x0a` | row count |
| `+0x2d` | format |
| `+0x2e` | format flags (`0x2`: 64-bit offsets, from format 4 on) |

**A lookup's result**: the id at `+0x0`, the row at `+0x8`; the four bytes between are left as
they were.

**Globals**: the built-in menu property rows (`0x05528730`), the built-in NewMenuColorTableParam
rows (`0x055286e0`), and where the DungeonFeatureParam lookup copies its header word
(`0x05992cb0`).

## Patch points

No patch points are recorded for these functions.

## Open questions

- The loader that reads each PARAM file and builds its search index is not replaced yet.
- What the header words at `+0x06` mean, and why the NpcParam and DungeonFeatureParam lookups pass
  them on.
- Why the row-name lookups exist in the retail build: they did not run in the coverage boots.
- Most of the header (beyond the row count, data version and format) is unknown.

Row layouts and field names come from the modding community's published parameter definitions;
they are cited as facts, not copied code.
