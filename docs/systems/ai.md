<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# AI

The game runs a shared Havok AI world, owned by the SprjHkAiManager singleton. This page covers the once-a-frame update that steps that world and draws its debug display.

**At a glance**

- **Scope:** The per-frame AI manager update: the world step and the AI debug drawing.
- **Verified:** `ai_hk_frame_update_0222bc10` steps the AI world by a fixed 1/30 s and gates the debug drawing behind flags.
- **Open:** The world step itself (`0x021cdad0`), the groups and objects, and how individual enemies decide what to do.

## Summary

The game steps a shared Havok AI world through `SprjHkAiManager` (singleton `0x059401a0`). The
verified wrapper does not establish which enemy or NPC decisions that world supplies.

### What one frame does

Once a frame, `ai_hk_frame_update_0222bc10` (`0x0222bc10`):

1. **Steps the Havok AI world** with a fixed time step of 1/30 s.
2. **Checks its second flag.** It is always set in the recordings. When it is set, the update:
    1. copies the frame object's matrices to the manager's view object;
    2. resets the render manager's debug state to its defaults and runs the AI debug display and
       drawing, made of the pieces below.

| Piece | What it uses |
|---|---|
| Debug position | the camera's position, or the player character's when the character manager exists |
| Debug marks | (not yet detailed) |
| Draw descriptor | every AI object in the owner's groups, or the whole manager |
| Axis frames | ids looked up in the manager's id map |
| Boxes | the box mode at `+0x2c68` |

The time step is a constant, not the measured frame time. Every frame moves the AI world on by
1/30 s however long the frame really took. That is why the frame-rate patches have to change it
(see [Patch points](#patch-points)).

In the recordings from Iosefka's Clinic the update resets its three groups, copies the matrices
and draws each AI object; the debug display, axis frames, boxes and the manager-wide draw stay off.

Scope still to map: the AI world step itself (`0x021cdad0`), the groups and objects, the
behaviour of individual enemies.

<details>
<summary>What this wrapper does not tell us</summary>

This verified wrapper establishes how the AI manager is stepped and how its debug drawing is
gated. It does not reveal an individual enemy's decisions, attack timing, or boss phases. The
current source and evidence contain no basis for those profiles yet; gameplay observations alone
will be labeled as observations until their implementation is traced.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0135d810` | 81 | `ai_debug_draw_axes_0135d810` | edge-verified | Takes the renderer lock (+0x28 slot 3, wait forever), calls renderer slot 5, draws the axis frame into the current slot (0x0135ec80), then tail-calls the lock release (slot 5) |
| `0x016243a0` | 74 | `ai_character_negated_third_column` | verified | AI view slot +0x58: the character transform's third column negated (w 0), zero vector without a character |
| `0x01624760` | 35 | `ai_character_id` | verified | AI view slot +0x90: the character's id through its vtable slot +0x88, -1 without a character |
| `0x01624e50` | 109 | `ai_character_flag_module_set` | verified | AI view slot +0x128: the owner's flag module (+0x30) is enabled (+0x384) and flag +0x37f or +0x380 is set |
| `0x0162d840` | 897 | `ai_target_speed_record_update` | verified | Target speed record: 30-sample ring of the followed target's speed along the line to the character, weighted average |
| `0x016334b0` | 804 | `ai_debug_set_position_016334b0` | edge-verified | Debug display move: stays while a point-gated object is within 1e-4 of the reference; else target=object, point->previous, finds the point near the object (0x021c97d0) or in the world |
| `0x01633ac0` | 734 | `ai_debug_flush_01633ac0` | edge-verified | Marks (debug flag 2) the objects of the entities the display object links to (+0x150, 0x60 apart): ids copied to a temporary std::list from the default allocator first |
| `0x0163bb50` | 626 | `ai_debug_end_0163bb50` | edge-verified | Debug view end of frame: draws its marks (and the world marks) when asked; then the subject bounding box (vtable +0x60) through 0x0135dca0 with the bounds render state |
| `0x01657ac0` | 91 | `ai_func_get_random_float` | edge-verified | AI script binding GetRandam_Float: min + (max - min) * 20-bit random fraction |
| `0x01657b20` | 90 | `ai_func_get_random_int` | verified | AI script binding GetRandam_Int: integer in [min, max] from a 20-bit random fraction (span wraps, truncated) |
| `0x01657b80` | 19 | `ai_func_set_timer` | verified | AI script binding SetTimer: think timer 0..15 (index checked signed, as the original) |
| `0x01657ba0` | 22 | `ai_func_set_number` | verified | AI script binding SetNumber: think script number 0..63 (index checked unsigned) |
| `0x01658770` | 205 | `ai_func_is_inside_target_ex` | edge-verified | IsInsideTargetEx: finds both characters, direction number -> heading (table) and target-line flag (5-8), then the inside test |
| `0x0165a1d0` | 32 | `ai_func_get_sp` | edge-verified | AI script binding GetSp: the named character's SP through its vtable, 0 without one |
| `0x0165a1f0` | 32 | `ai_func_get_mp` | edge-verified | AI script binding GetMp: the named character's MP through its vtable, 0 without one |
| `0x0165a210` | 32 | `ai_func_get_hp` | edge-verified | AI script binding GetHp: the named character's HP through its vtable, 0 without one |
| `0x0165a230` | 34 | `ai_func_get_hp_rate` | edge-verified | AI script binding GetHpRate: the named character's HP fraction through its vtable, 0.0 without one |
| `0x0165aff0` | 23 | `ai_func_get_timer` | verified | AI script binding GetTimer: think timer 0..15, 0.0 above |
| `0x0165b010` | 28 | `ai_func_is_finish_timer` | verified | AI script binding IsFinishTimer: think timer <= 0 (true above index 15) |
| `0x0165b030` | 26 | `ai_func_get_number` | verified | AI script binding GetNumber: think script number 0..63, 0.0 above |
| `0x0165baf0` | 22 | `ai_func_is_battle_state` | verified | AI script binding IsBattleState: perception target state == 3 (battle) |
| `0x0165bb10` | 27 | `ai_func_is_change_state` | verified | AI script binding IsChangeState: target state differs from last frame's |
| `0x0165bb30` | 22 | `ai_func_is_caution_state` | verified | AI script binding IsCautionState: perception target state == 1 (caution) |
| `0x0165bb50` | 22 | `ai_func_is_find_state` | verified | AI script binding IsFindState: perception target state == 2 (find) |
| `0x0165bb70` | 29 | `ai_func_get_current_target_state` | edge-verified | AI script binding GetCurrTargetState: forced state (think +0x9a8) unless -1, else the perception's |
| `0x0165bb90` | 18 | `ai_func_get_previous_target_state` | verified | AI script binding GetPrevTargetState: the perception's target state last frame |
| `0x0165ca50` | 130 | `ai_func_is_visible_target` | edge-verified | IsVisibleTarget: stores the player's set entry; selectors -2,-1,20-22,24,25 and points 100-123 are visible, else the current target's visible flag |
| `0x0167f2a0` | 22 | `ai_goal_func_set_timer` | edge-verified | AI goal script binding SetTimer: goal timer 0..15 (index checked signed) |
| `0x0167f2d0` | 22 | `ai_goal_func_set_number` | edge-verified | AI goal script binding SetNumber: goal script number 0..63 (index checked unsigned) |
| `0x0167f820` | 26 | `ai_goal_func_get_timer` | edge-verified | AI goal script binding GetTimer: goal timer 0..15, 0.0 above |
| `0x0167f840` | 32 | `ai_goal_func_is_finish_timer` | edge-verified | AI goal script binding IsFinishTimer: goal timer <= 0 (false above index 15) |
| `0x0167f860` | 20 | `ai_goal_func_is_finish_turn_timer` | edge-verified | AI goal script binding IsFinishTimerForTurnBeforeAtk: the turn-before-attack timer <= 0 |
| `0x0167f880` | 26 | `ai_goal_func_get_number` | edge-verified | AI goal script binding GetNumber: goal script number 0..63, 0.0 above |
| `0x01693850` | 1175 | `ai_is_inside_direction` | verified | Inside test: 3D distance <= limit and flat angle between direction and target line below half the cone (cos series) |
| `0x0169acf0` | 114 | `ai_character_has_effect_state` | verified | AI view slot +0x30: an active effect-list entry's row sets state 0x27, 0xb0 or 0xb1 (row bits 0x162:6, 0x162:7, 0x163:0) |
| `0x0169ad80` | 134 | `ai_character_gather_effect_states` | verified | AI view slot +0x40: ORs states 0x27/0xb0/0xb1 of every active effect-list entry into three bytes |
| `0x0169af00` | 126 | `ai_character_ready_check` | edge-verified | AI view slot +0x68: module +0x78 owner: mode word == 1 when queries 0x1a8 yes / 0x1c0 no, else AI state slot != 0xff |
| `0x0169fad0` | 155 | `ai_character_link_ids` | verified | The two ids of the character's link (vtable +0x108, +0x1f8) when its partner's link module is active and unblocked; else -1, -1 |
| `0x016a52b0` | 1936 | `ai_sight_test` | verified | Sight test: nose distance at any angle, else eye distance by darkness from a moved-back eye point, begin distance, width/height cone, optional line-of-sight ray (layer 0x29); unseeable flag wins |
| `0x016a5a50` | 43 | `ai_sight_query_reset_target` | edge-verified | Sight query: target half reset (origin, no height or size, visibility 1, light, seeable) |
| `0x016a85c0` | 1463 | `ai_perception_update` | edge-verified | Perception update per think: target-list update, then target state and type from the list (kind 4/5 battle, main entry's state, kind 3/11 caution), facing check, force battle |
| `0x016acfa0` | 384 | `ai_perception_current_handle` | verified | Perception: handle of the current target (kind-4 node, kind-5 node, main entry, kind-3 node), character or place |
| `0x016ad400` | 287 | `ai_perception_target_visible` | verified | Target list: visible flag of the first kind-4 node, else true for kind 5, else the main entry's flag, else true for kind 3 or 11 |
| `0x016b2760` | 512 | `ai_target_list_refresh_noticed` | verified | Target list: refresh the noticed character's entry (clock 0, visible, refreshed bit) or allocate and link a new 0xa0-byte entry; notice record update |
| `0x016b30a0` | 368 | `ai_target_list_mark_main_matches` | verified | Target list: marks entries matching the highest-priority entry (same character id, or place within 0.5 m); 8 s recheck timer |
| `0x01708410` | 168 | `ai_pending_event_fire` | verified | Pending AI event: character by entity id has this event id pending: post kind 0x1d, clear it; a failed lookup clears the cached index |
| `0x021d6ea0` | 199 | `ai_draw_world_021d6ea0` | edge-verified | No-op without the view renderer (+0x8); sets slot state +0x18=2 +0x1c=0 +0x70=3 +0x78=16.0 (dirty 4/8/0x80, +9 bit 1) when they differ, then 0x021d5960 per section of world+0x50 list |
| `0x02224090` | 573 | `ai_group_reset_02224090` | verified | World area debug step: its debug menu entry state machine (+0x18: add, shown, remove, hidden) then, with arg 2, every block stepped (0x02224a60) or its skip flag (+0x9c) cleared |
| `0x0222bc10` | 2693 | `ai_hk_frame_update_0222bc10` | verified | SprjHkAiManager per-frame update: steps the AI world by 1/30 s (the 60 FPS patches: 1/60); AI debug display and drawing |
| `0x02232a60` | 104 | `ai_draw_layer_color_02232a60` | edge-verified | Draw colour callback: default (0x04d17e50) when the requested mask is 0, else the mean of palette 0x04b35040 colours for bits 0-19 set in both masks, alpha 1 |
| `0x029ae440` | 418 | `ai_entity_point_029ae440` | verified | Face point of a nav-mesh-like source: mean of its edges start vertices through the transform at +0x70 (face/edge maps, added faces/edges/vertices), times refined rcpps of the count; writes 16 bytes |

## Structs and globals

**Owner of the update.** The AI objects sit two levels down: the owner holds groups, each group
holds elements, and each element holds a list of objects.

| Offset | Field |
|---|---|
| `+0x2bc8` | group count |
| `+0x2bd0` | groups, `0x20` bytes each: element count `+0x8`, elements `+0x10` |
| (element) | `0xb0` bytes each: object count `+0x58`, object list `+0x60` |
| `+0x2c18`, `+0x2c20` | debug display |
| `+0x2c68` | box mode |
| `+0x2c90`..`+0x2c98` | flags |
| `+0x2cb8` | axis-frame pairs (id, index), `+0x2ec0` of them |

**`SprjHkAiManager`.** AI world `+0x10` (its id map at `+0x10`), view object `+0x20`.

## Patch points

The AI world's time step is 1/30 s. The frame-rate patches are described in
[Frame timing](frame_timing.md).

| Patch | AI world step |
|---|---|
| original, `30 FPS++`, `Uncap FPS++` | 1/30 s |
| `60 FPS++`, `90 FPS++`, `60FPS (no deltatime)` | 1/60 s |
| `BB_TARGET_FPS=60` | 1/60 s |

## Open questions

- With `Uncap FPS++`, the AI world still steps by 1/30 s each frame: does AI run faster at high
  frame rates?
