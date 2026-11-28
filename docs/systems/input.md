<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Input

The input system turns the controller's buttons and sticks into the game's own logical keys. This page covers the key assign that answers "is this key pressed?", the once-a-frame step of FD4PadManager, and the pad vibration and mouse setup functions.

**At a glance**

- **Scope:** Pad polling, mapping, dead zones, vibration, mouse and input buffering. Mapped so far: the key assign's queries and its key mapper, the per-frame step of the `FD4PadManager` pad map, pad vibration and the mouse device's setup.
- **Verified:** The pad step, the vibration setter, the key assign's pressed, analog and free-id queries and the pad slot constructor are verified. The key mapper's lookups, the held query, the mouse device constructor and its two opens are edge-verified.
- **Open:** What the two entries and the owner are; where the pad is polled and how its state reaches the key assign; dead zones and input buffering.

## Summary

`FD4PadManager` (singleton `0x058b31f0`) keeps a map of pad entries. Once a frame,
`input_pad_step_01972900` (`0x01972900`):

1. **Marks two pad entries**, keyed by the addresses of two globals (`0x059464a8`,
   `0x059566c4`).
2. **Checks whether its owner is ready**: `+0xc4` is 1, `+0xc8` is 0 and `+0xd0` is set.
3. **Advances that owner** with a frame-time descriptor, when it is ready.

While the hunter stands in Iosefka's Clinic the advance branch stays idle.

Scope still to map: pad polling, dead zones, input buffering.

### From buttons to logical keys

The game does not ask "is the cross button down?". It asks about its own **logical keys** (menu
up, confirm and so on), and the FD4 key assign answers from the physical buttons and sticks each
key is assigned to. These queries run hundreds of thousands of times in a boot, on the title menus,
in character creation and in the clinic.

- **Pressed and analog.** A logical key is answered only when the key assign is enabled, the key
  mapper gives a button for it, and the key's state entry says it is enabled and not suppressed.
  "Pressed" is that button's bit in the sampled state, or else the alternate input the mapper names
  for that button being held; "analog" is the button's value (a stick axis or a pressure), 0
  otherwise. The original looks the key's state entry up twice, once per flag, and the replacement
  does the same.
- **Held combinations.** A key can also be "held" through any of its combinations, each of up to
  four logical keys with a test (pressed, released, or an analog value above 0). An unused part
  passes; a part with an unknown test repeats the previous part's result, so as the first part it
  fails, because the original leaves its result register untouched.
- **The key mapper** answers which button index a logical key is assigned to, and which second
  input (another device's) a button index maps to, or -1 for none. Its maps are MSVC trees with
  the key and value packed at 4-byte alignment.
- **Bookkeeping.** A search finds the first free id among a table's assignments (visiting every
  entry for each id), and a new pad slot starts with no pad and a "pad not set" debug label.

<details>
<summary>Key assign: PRs and evidence</summary>

- [#995](https://github.com/Yharnam-Hunters/Paleblood/pull/995): the pressed and analog
  queries, the free-id search and the pad slot constructor, now **verified**: checked against
  recordings from the game and approved by an independent review.
- [#1063](https://github.com/Yharnam-Hunters/Paleblood/pull/1063): the key mapper's two
  lookups, the held-set test and the held query, **edge-verified** from the code and
  constructed tests (29 to 38 cases each; all 18 deliberately wrong versions fail).

</details>

### Scripted walks for test boots

Unattended test boots drive the game with input scripts. Since
[#975](https://github.com/Yharnam-Hunters/Paleblood/pull/975) a script can **walk** the character
to a ground position: the runtime steers the left stick towards the target, re-aiming every few
frames from where the character really is, learns the camera's turn from how the character moved,
sidesteps when stuck, and gives up after a time limit. The position comes from a small game-side
function (`game/input/player_position.cpp`) that reports the player position the Havok AI debug
display already reads, or nothing while a map loads. It is test tooling, not a replacement:
nothing in the game calls it. In a clinic boot, a route of 12 walks took the player from the
operating table to the enemy.

### What the game asks the system for

The executable's imports show which input libraries it uses, and which calls from each:

| Library | Calls the game imports | What that tells us |
|---|---|---|
| `libScePad` | `scePadInit`, `scePadOpen`, `scePadClose`, `scePadReadState`, `scePadGetControllerInformation`, `scePadSetVibration`, `scePadResetOrientation`, `scePadSetAngularVelocityDeadbandState`, `scePadSetTiltCorrectionState` | The game reads the pad's latest state once per poll (`scePadReadState`), not the buffered history (`scePadRead` is not imported). It drives vibration and sets up the motion sensor. |
| `libSceMouse` | `sceMouseInit`, `sceMouseOpen`, `sceMouseClose`, `sceMouseRead` | A mouse path exists, set up by the mouse device functions around `0x02a8c000` (below). |
| `libSceUserService` | `sceUserServiceInitialize`, `sceUserServiceGetInitialUser`, `sceUserServiceGetLoginUserIdList`, `sceUserServiceGetUserName`, `sceUserServiceGetEvent`, `sceUserServiceTerminate` | Pads and the mouse are opened for a user, starting with the user who launched the game. |
| `libSceImeDialog` | `sceImeDialogInit`, `sceImeDialogGetStatus`, `sceImeDialogGetResult`, `sceImeDialogAbort`, `sceImeDialogTerm` | Typed text (the character name, for example) goes through the system keyboard dialog; there is no keyboard library. |

**What the game's pad calls show.** Paleblood's runtime answers these calls ([#373](https://github.com/Yharnam-Hunters/Paleblood/pull/373),
[#431](https://github.com/Yharnam-Hunters/Paleblood/pull/431); see Runtime system calls), and the
executable's own calls fix their shapes:

- before each `scePadReadState` (`0x02a90168`, `0x02a91948`) it clears the 120-byte state block,
  and afterwards it tests the "connected" byte at offset `0x4c`;
- it reads the touch pad's resolution and the sticks' deadzones from
  `scePadGetControllerInformation`, and treats a stick within `0x80` ± that deadzone as centred;
- `scePadInit` is called once (`0x02fa6182`) and its result is not read; vibration is set at
  `0x02a9c7c1` and stopped at `0x02a9c822`.

With `--input host` the runtime feeds a real gamepad and the keyboard into these calls. That is the
runtime's side; the game code that reads the state is not decompiled yet.

### Vibration and the mouse

Four small device functions are replaced (Paleblood
[#520](https://github.com/Yharnam-Hunters/Paleblood/pull/520)); the vibration setter is now
verified, the three mouse functions edge-verified:

- **Vibration.** `input_pad_set_vibration` (`0x02a9c790`) sets both motors of an open pad with
  `scePadSetVibration`, and answers false when the pad is not open or the call fails. Its second
  argument drives the small motor and its third the large one (the order of
  `ScePadVibrationParam` is large, then small). Its one caller is `0x01395ca5`.
- **The mouse device.** `input_mouse_construct` (`0x02a8c210`) builds a mouse device on top of the
  input devices' shared base constructor (`0x02a8d2b0`): it loads the mouse system module (id
  `0xa9`), initialises the mouse library and opens the mouse through the device's own virtual
  open. If any step fails, the device keeps handle 0.
- **Opening for the initial user.** That open, `input_initialize_mouse_for_initial_user`
  (`0x02a8c420`, named by [@gabee12](https://github.com/gabee12) in
  [#270](https://github.com/Yharnam-Hunters/Paleblood/pull/270)), asks the user service for the
  user who started the game, closes the previous connection when its handle is positive, and
  opens the mouse for that user (type 0, index 0). It is true when the new handle is positive.
  A second mouse device class has an identical open, `input_mouse_b_open_for_initial_user`
  (`0x02a8bf30`), with its handle at `+0x128` instead of `+0x168`.

<details>
<summary>How these were verified</summary>

Each function's edge cases pass against the original (9, 10, 11 and 11 cases), deliberately
wrong versions fail, and an independent review approved all four. No community patch edits
them. The vibration setter has since been checked against recordings from the game; the three
mouse functions are edge-verified, from the code and constructed tests only.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0138d060` | 82 | `input_key_mapper_alternate_input_0138d060` | edge-verified | Key mapper: alternate input of a button index, std::map<int32,int32> find on the map at +0x48 (head +0x50); -1 when missing |
| `0x01390ba0` | 82 | `input_key_mapper_button_index_01390ba0` | edge-verified | Key mapper: button index of a logical key, std::map<int32,int32> find on the map at +0x40 (head at +8); -1 when missing |
| `0x013914a0` | 142 | `input_pad_slot_construct_013914a0` | verified | Pad slot constructor (vtable 0x056f1be0): label "pad not set", +0x24..+0x50 cleared, empty wide name at +0x50 (owns 1), +0x88 cleared |
| `0x01391c60` | 442 | `input_key_assign_held_01391c60` | edge-verified | Key assign held: any combination of the key in the multimap at +0x38 whose four parts pass (key -1 passes; 0 pressed, 1 released, 2 analog > 0; other tests repeat the previous part) |
| `0x01391e30` | 299 | `input_key_assign_pressed_01391e30` | verified | Key assign pressed: mapper (+0x28) button index; enabled +0x20; key state map +0x40 enabled and not suppressed; pressed bit, else alternate input in the held set |
| `0x01391f60` | 215 | `input_key_assign_analog_value_01391f60` | verified | Key assign analog value: as the pressed query (no mapper null check), then the input state float at the button index; 0 otherwise |
| `0x013983e0` | 192 | `input_key_assign_first_free_id_013983e0` | verified | Smallest id 0-7 that no assignment (id at +0x24) of table +0x38[table] uses, every entry visited per id; 0xffffffff when all taken |
| `0x013994a0` | 87 | `input_held_set_contains_013994a0` | edge-verified | Held input set: std::map<int32,bool> find (head at +0x18); 1 when present with a nonzero flag, else 0 |
| `0x01972900` | 426 | `input_pad_step_01972900` | verified | FD4PadManager map: marks two entries; advances an owner by the frame-time step (patched to 1/15 or 1/240 s; the advance branch is idle in the clinic) |
| `0x02a8bf30` | 108 | `input_mouse_b_open_for_initial_user` | edge-verified | Second mouse device class's open: initial user; close a positive handle at +0x128; sceMouseOpen(user; 0; 0; null); true when the new handle is positive |
| `0x02a8c210` | 164 | `input_mouse_construct` | edge-verified | Mouse device constructor: base ctor with the third argument; vtable 0x056b7d00; clear 0x140..0x177; load sysmodule 0xa9; sceMouseInit; open via slot 32; handle 0 on any failure |
| `0x02a8c420` | 108 | `input_initialize_mouse_for_initial_user` | edge-verified | Initializes or reinitializes mouse input for the initial user; closing a previous connection if necessary. |
| `0x02a9c790` | 84 | `input_pad_set_vibration` | verified | scePadSetVibration on the pad's handle (+0x8) if open (+0xc): large motor from the third argument; small from the second; true on success |

## Structs and globals

**The pad map** is an MSVC `std::map` at `FD4PadManager->+0x38->+0x8`. Keys are compared as
signed 64-bit numbers. Each node:

| Offset | Field |
|---|---|
| `+0x0` | left |
| `+0x10` | right |
| `+0x19` | nil flag |
| `+0x20` | key |
| `+0x28` | value |
| `+0x30` | a flag set when the entry is used |

## Patch points

The advance's step is 1/30 s. The frame-rate patches (see [Frame timing](frame_timing.md))
change it:

| Patch | Advance step |
|---|---|
| original, `90 FPS++` | 1/30 s |
| `30 FPS++`, `60 FPS++` | 1/15 s |
| `Uncap FPS++` | 1/240 s |

`BB_TARGET_FPS` does the same.

## Open questions

- What the two pad map entries keyed by globals are, and what the owner the step advances is.
- What the functions around the two `scePadReadState` calls (`0x02a90168`, `0x02a91948`) keep of
  each state (the start of pad polling).
- How the sampled pad state reaches the key assign, and input buffering, have not been studied yet. The sticks' dead zones are applied by
  the game's own code, from the deadzone the controller information reports (only the motion
  sensor's deadband is set through the library).
- Whether the mouse path is used by anything in the shipped game: the mouse device's constructor has two callers (`0x02558eb3`, `0x02558fb6`), not yet mapped, and nothing calls either open directly (only their vtable slots point at them).
- What the shared input device base constructor (`0x02a8d2b0`, six callers) sets up, and which other device classes use it.
