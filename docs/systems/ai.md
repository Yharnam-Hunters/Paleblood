<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# AI

How enemies notice you and how their AI is run. The game steps a shared Havok AI world once a frame; each enemy's AI scripts call back into the game to read timers, numbers and stats; and the engine decides what each enemy sees and hears. This page covers the parts of that already rewritten as readable source.

**At a glance**

- **Scope:** The per-frame AI world update and its debug drawing; the script bindings an enemy's AI scripts call; sight, hearing and the target list; the first pieces of the goal framework.
- **Verified:** The frame update, the sight test, the perception update, the target list's main-target matching, most per-frame character queries and most timer, number and target-state bindings are checked against recordings from the game.
- **Edge-verified:** Hearing (the ear's shape, the hearing test and the sound check), the goal registration options and the update-time scheduler, the stat bindings and several debug-draw helpers: checked from the code and constructed tests only.
- **Open:** The AI world step itself (`0x021cdad0`), the target-list update that ages and promotes entries, how goals are run, and how any individual enemy decides what to do.

## Summary

The game steps a shared Havok AI world through `SprjHkAiManager` (singleton `0x059401a0`).
Enemy behaviour itself is written in AI scripts that run on the game's script VM; each frame they
call back into the executable through methods registered under the names the scripts use.
Paleblood has replaced the engine side of several of those calls and the perception code they read
from. None of this yet describes how a particular enemy or boss chooses its attacks.

### What one frame does

Once a frame, `ai_hk_frame_update_0222bc10` (`0x0222bc10`):

1. **Steps the Havok AI world** with a fixed time step of 1/30 s.
2. **Checks its second flag.** It is always set in the recordings. When it is set, the update:
    1. copies the frame object's matrices to the manager's view object;
    2. resets the render manager's debug state to its defaults and runs the AI debug display and
       drawing, made of the pieces below.

| Piece | What it does |
|---|---|
| Debug position | the camera's position, or the player character's when the character manager exists |
| Group reset (`0x02224090`) | one world area's debug step: its debug-menu entry cycles through added, shown, removed and hidden; with the step flag set, the area's blocks are stepped too |
| Debug display move (`0x016334b0`) | moves the display to a position, staying put while its object stands on the same unflagged navigation point close to the reference |
| Debug flush (`0x01633ac0`) | marks the objects of the entities the display's object links to |
| Debug end (`0x0163bb50`) | draws the view's marks and the world's marks, then the subject's bounding box when asked (a box whose maximum is below its minimum in any lane is skipped) |
| Draw descriptor | every AI object in the owner's groups, or the whole manager; its colour callback (`0x02232a60`) averages the palette colours of the layer bits both masks share |
| World draw (`0x021d6ea0`) | sets four render-state settings, then draws every section of the AI world (nothing when the view has no debug renderer) |
| Axis frames (`0x0135d810`) | ids looked up in the manager's id map, each drawn under the debug renderer's lock |
| Entity point (`0x029ae440`) | the centre of one face of a nav-mesh instance, through its transform |
| Boxes | the box mode at `+0x2c68` |

The time step is a constant, not the measured frame time. Every frame moves the AI world on by
1/30 s however long the frame really took. That is why the frame-rate patches have to change it
(see [Patch points](#patch-points)).

In the recordings from Iosefka's Clinic the update resets its three groups, copies the matrices
and draws each AI object; the debug display, axis frames, boxes and the manager-wide draw stay off.

<details>
<summary>The frame update's helpers: PRs, contributors and evidence</summary>

- [@FFwLo](https://github.com/FFwLo) replaced the debug-draw helpers:
  [#815](https://github.com/Yharnam-Hunters/Paleblood/pull/815) (the axis frames and the
  layer colour callback), [#885](https://github.com/Yharnam-Hunters/Paleblood/pull/885) (the
  world draw and the entity point) and
  [#972](https://github.com/Yharnam-Hunters/Paleblood/pull/972) (the group reset and the debug
  display's move, flush and end).
- The group reset, the entity point and the debug display move are now **verified**: checked
  against recordings from the game (the move's in
  [#1079](https://github.com/Yharnam-Hunters/Paleblood/pull/1079), with all of its paths
  covered by recordings and constructed cases together) and approved by an independent
  review.
- The axis frames, colour callback, world draw, flush and end are **edge-verified**: from the
  code and constructed tests only. The clinic boots never call some of them, and others start
  with a short branch the recorder cannot yet hook.

</details>

### What an enemy's scripts can ask

The engine side of the AI scripts is a set of small bindings that read or change a character's
"think", the object that carries its AI state
([#911](https://github.com/Yharnam-Hunters/Paleblood/pull/911)):

- **Random draws.** A float or an integer in a range, from a 20-bit fraction of the engine's random
  source.
- **Timers and numbers.** Each think keeps 16 count-down timers and 64 free numbers, separately for
  the logic scripts and the goal scripts. The two sides check their indices differently (timers
  signed, numbers unsigned) and answer an out-of-range "is this timer finished?" differently; the
  replacements keep those quirks.
- **Target state.** Four levels: normal, caution, find and battle, plus whether the state changed
  since the last think, and an optional forced state.
- **Stats.** The SP, MP, HP and HP ratio of the character the script names, or 0 when there is
  none.

Alongside them run the per-frame questions a think asks about its own character
([#920](https://github.com/Yharnam-Hunters/Paleblood/pull/920)): its id, a negated column of its transform, a flag-module check, whether an
effect on it carries one of three state bits, a ready check whose meaning is not yet known, link
ids, and a pending AI event that is posted once the named character has it pending.

The random integer, the timer, number and target-state bindings and the character queries are
**verified** from in-game recordings; the random float, the forced-state read, the stat bindings and
the goal-side timers and numbers are **edge-verified**.

### How an enemy sees you

Sight decides the "visible" flag the scripts read
([#936](https://github.com/Yharnam-Hunters/Paleblood/pull/936),
[#958](https://github.com/Yharnam-Hunters/Paleblood/pull/958)). In plain words:

- **Close by, at any angle.** A character within the enemy's "nose" distance (plus both bodies'
  sizes) is noticed whichever way the enemy faces.
- **By eye.** Further out, the target must be within an eye distance that depends on how dark it
  is where the target stands, scaled by how visible the target is and how sharp the enemy's sight
  is; measured from a point a little behind the enemy, and not closer than a starting distance.
  It must also be inside the enemy's width cone and height cone.
- **Line of sight.** When the caller asks, a ray from the enemy's eyes to the target's must hit
  nothing in the physics world.
- A target marked unseeable is never noticed, though the test still runs.

The sight test also places targets by direction ("is the target in front, behind, beside me?",
`0x01693850`), and a speed record keeps how fast the followed target is closing in over the last
30 frames.

**The target list.** What an enemy has noticed goes into a list of target entries. A noticed
character refreshes its entry (marked visible this update) or gets a new one
(`0x016b2760`). The entry with the highest priority is the main target, and every other entry for
the same character, or for a place within half a metre of it, is marked as matching it
(`0x016b30a0`, [#920](https://github.com/Yharnam-Hunters/Paleblood/pull/920),
[#951](https://github.com/Yharnam-Hunters/Paleblood/pull/951),
[#961](https://github.com/Yharnam-Hunters/Paleblood/pull/961)).

**The perception update** (`0x016a85c0`, [#999](https://github.com/Yharnam-Hunters/Paleblood/pull/999))
runs once per think. It counts down a perception timer, runs the target-list update (sight and
hearing; still the original code), and then reads the target state off the list in a fixed order.
The normal, caution, find and battle ladder lives in each target entry; this update only reports
it, and the state it replaces becomes the previous state, so "the state changed" is true for exactly
one think. It also answers whether the target faces the enemy and serves a one-shot "force battle"
request.

<details>
<summary>Sight and perception: what is verified</summary>

- **Verified** (checked against recordings from the game, each approved by an independent
  review): the sight test, the target-list refresh, the visible-flag lookup and the direction
  test ([#997](https://github.com/Yharnam-Hunters/Paleblood/pull/997)); the main-target
  matching ([#951](https://github.com/Yharnam-Hunters/Paleblood/pull/951), whose recordings
  only held empty lists, so its matching logic rests on constructed cases, including the
  boundary case added in [#961](https://github.com/Yharnam-Hunters/Paleblood/pull/961)); the
  perception update and the current target's handle
  ([#1079](https://github.com/Yharnam-Hunters/Paleblood/pull/1079), from a capture boot of
  the clinic beast fight: 1493 of 1503 recordings pass, and 10 are set aside because the
  original itself leaves stack garbage in one byte or the recording is incomplete); the
  target speed record.
- **Edge-verified** (from the code and constructed tests): the script-side visible-target and
  inside-target queries and the sight query's reset.

</details>

### How an enemy hears you

Hearing is replaced in three steps, all **edge-verified** from the code and constructed tests
([#1067](https://github.com/Yharnam-Hunters/Paleblood/pull/1067),
[#1089](https://github.com/Yharnam-Hunters/Paleblood/pull/1089),
[#1103](https://github.com/Yharnam-Hunters/Paleblood/pull/1103)). An enemy does not hear "noise"
in general: each sound has a position, a range and a maker.

1. **The sound check** (`0x0169cef0`) first decides whether this enemy may hear that maker at all,
   and how big the maker is. A character's sound is checked by team, using a table of which teams
   hear which (some sounds use the attack-hit team tables instead). An object's sound counts while
   the object still exists. Either way the sound then goes to the hearing test.
2. **The hearing test** (`0x0169d160`) compares the sound's range, less the enemy's sound-cut
   distance, with the gap between them: the distance less both bodies' sizes. If the range covers
   the gap, the sound is heard.
3. Otherwise the enemy's **ear** may still reach it: the ear reaches a little further, and its
   width and height angles decide. **The ear's shape** (`0x0169da30`) answers whether a sphere
   around the sound reaches the ear, within its height angle or along that angle's edge.

A heard sound is remembered: its position (unless the enemy already remembers a closer one) and,
always, which sound it was.

<details>
<summary>Why hearing is not verified yet</summary>

Each function passes its constructed cases against the original (449, 561 and 1642), and the
deliberately wrong versions fail, except ones shown to be equivalent. The ear's shape cannot be
recorded yet: one of its arguments arrives in a vector register the recorder does not capture.
No recording from the game covers any of the three.

</details>

### Goals: the first pieces

An enemy's AI plans with goals: its scripts add goals by id, each goal id having a definition
registered when the scripts load. The first part of that framework is replaced and
**edge-verified** ([#1109](https://github.com/Yharnam-Hunters/Paleblood/pull/1109)):

- **Registration options.** Four script functions the AI scripts call at load for a goal id. Each
  finds the id's definition and sets one option: the goal has no update, the update-time range it
  is updated in, the goal cannot be interrupted, or combo attacks may be cancelled.
- **The update-time scheduler's reserve** (`0x016a3be0`). It spreads AI updates over frames: a
  ring of 100 one-frame slots, at 30 frames a second, each counting the updates booked in it. A
  booking goes into the slot its delay names, or, when that slot is full, into the next slot with
  room, and the delay comes back moved to match. Nothing in the executable calls it directly.

How goals are run, interrupted and ended is not replaced yet.

<details>
<summary>What this page does not tell us</summary>

The replaced code establishes how the AI world is stepped, what the scripts can read, and how
sight, hearing and the target state are computed. It does not reveal any individual enemy's
decisions, attack timing or boss phases: those live in the AI scripts and the goal code that is
not replaced yet. Gameplay observations alone will be labelled as observations until their
implementation is traced.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0135d810` | 81 | `ai_debug_draw_axes_0135d810` | edge-verified | Takes the renderer lock (+0x28 slot 3, wait forever), calls renderer slot 5, draws the axis frame into the current slot (0x0135ec80), then tail-calls the lock release (slot 5) |
| `0x016243a0` | 74 | `ai_character_negated_third_column` | verified | AI view slot +0x58: the character transform's third column negated (w 0), zero vector without a character |
| `0x01624760` | 35 | `ai_character_id` | verified | AI view slot +0x90: the character's id through its vtable slot +0x88, -1 without a character |
| `0x01624e50` | 109 | `ai_character_flag_module_set` | verified | AI view slot +0x128: the owner's flag module (+0x30) is enabled (+0x384) and flag +0x37f or +0x380 is set |
| `0x0162d840` | 897 | `ai_target_speed_record_update` | verified | Target speed record: 30-sample ring of the followed target's speed along the line to the character, weighted average |
| `0x016334b0` | 804 | `ai_debug_set_position_016334b0` | verified | Debug display move: stays while a point-gated object is within 1e-4 of the reference; else target=object, point->previous, finds the point near the object (0x021c97d0) or in the world |
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
| `0x0167b6b0` | 93 | `ai_func_register_goal_no_update` | edge-verified | REGISTER_GOAL_NO_UPDATE: sets the no-update option of a goal id's definition |
| `0x0167b710` | 118 | `ai_func_register_goal_update_time` | edge-verified | REGISTER_GOAL_UPDATE_TIME: sets the range of a goal id's script Update interval |
| `0x0167b790` | 93 | `ai_func_register_goal_no_interrupt` | edge-verified | REGISTER_GOAL_NO_INTERUPT: sets the no-interrupt option of a goal id's definition |
| `0x0167b7f0` | 81 | `ai_func_enable_combo_attack_cancel` | edge-verified | ENABLE_COMBO_ATK_CANCEL: enables combo attack cancel for a goal id |
| `0x0167f2a0` | 22 | `ai_goal_func_set_timer` | edge-verified | AI goal script binding SetTimer: goal timer 0..15 (index checked signed) |
| `0x0167f2d0` | 22 | `ai_goal_func_set_number` | edge-verified | AI goal script binding SetNumber: goal script number 0..63 (index checked unsigned) |
| `0x0167f820` | 26 | `ai_goal_func_get_timer` | edge-verified | AI goal script binding GetTimer: goal timer 0..15, 0.0 above |
| `0x0167f840` | 32 | `ai_goal_func_is_finish_timer` | edge-verified | AI goal script binding IsFinishTimer: goal timer <= 0 (false above index 15) |
| `0x0167f860` | 20 | `ai_goal_func_is_finish_turn_timer` | edge-verified | AI goal script binding IsFinishTimerForTurnBeforeAtk: the turn-before-attack timer <= 0 |
| `0x0167f880` | 26 | `ai_goal_func_get_number` | edge-verified | AI goal script binding GetNumber: goal script number 0..63, 0.0 above |
| `0x01693850` | 1175 | `ai_is_inside_direction` | verified | Inside test: 3D distance <= limit and flat angle between direction and target line below half the cone (cos series) |
| `0x0169acf0` | 114 | `ai_character_has_effect_state` | verified | AI view slot +0x30: an active effect-list entry's row sets state 0x27, 0xb0 or 0xb1 (row bits 0x162:6, 0x162:7, 0x163:0) |
| `0x0169ad80` | 134 | `ai_character_gather_effect_states` | verified | AI view slot +0x40: ORs states 0x27/0xb0/0xb1 of every active effect-list entry into three bytes |
| `0x0169af00` | 126 | `ai_character_ready_check` | verified | AI view slot +0x68: module +0x78 owner: mode word == 1 when queries 0x1a8 yes / 0x1c0 no, else AI state slot != 0xff |
| `0x0169cef0` | 607 | `ai_hearing_check_sound` | edge-verified | Hearing: who made a sound (character or object by handle, or neither) and whether the listener's team hears the maker's; calls the hearing test with the maker's size |
| `0x0169d160` | 2248 | `ai_hearing_test` | edge-verified | Hearing: whether the enemy hears one sound (range less the sound-cut distance against the gap; otherwise the ear's reach by width and height angle) and remembers it |
| `0x0169da30` | 763 | `ai_hearing_in_ear_range` | edge-verified | Hearing: does a sphere around a sound point reach the ear (within the ear height angle: reach plus radius; beyond it: the sphere surface crosses the angle edge segment) |
| `0x0169fad0` | 155 | `ai_character_link_ids` | verified | The two ids of the character's link (vtable +0x108, +0x1f8) when its partner's link module is active and unblocked; else -1, -1 |
| `0x016a3be0` | 154 | `ai_update_time_scheduler_reserve` | edge-verified | AI update-time scheduler: books an update in a 100-frame ring, moving it to the first later frame below the per-frame limit |
| `0x016a52b0` | 1936 | `ai_sight_test` | verified | Sight test: nose distance at any angle, else eye distance by darkness from a moved-back eye point, begin distance, width/height cone, optional line-of-sight ray (layer 0x29); unseeable flag wins |
| `0x016a5a50` | 43 | `ai_sight_query_reset_target` | edge-verified | Sight query: target half reset (origin, no height or size, visibility 1, light, seeable) |
| `0x016a85c0` | 1463 | `ai_perception_update` | verified | Perception update per think: target-list update, then target state and type from the list (kind 4/5 battle, main entry's state, kind 3/11 caution), facing check, force battle |
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

**The think's perception record** sits at think `+0x560`; the script bindings read the current and
previous target state from it, and a forced state at think `+0x9a8` (-1 when none).

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
- How the target-list update ages entries and moves them up the caution, find and battle ladder.
- How goals are activated, updated, interrupted and ended, and what the update-time scheduler's
  owner books with it.
- What the ready check among the character queries stands for.
