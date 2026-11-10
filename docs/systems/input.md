<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Input

The input system handles the controller pads. This page covers the once-a-frame step of FD4PadManager, the manager that keeps a map of pad entries, and the pad vibration and mouse setup functions.

**At a glance**

- **Scope:** Pad polling, mapping, dead zones, vibration, mouse and input buffering. Mapped so far: the per-frame step of the `FD4PadManager` pad map, pad vibration and the mouse device's setup.
- **Verified:** The pad step is verified; the vibration setter, the mouse device constructor and its two opens are edge-verified.
- **Open:** What the two entries and the owner are; where the pad is polled; mapping, dead zones and input buffering.

## Summary

`FD4PadManager` (singleton `0x058b31f0`) keeps a map of pad entries. Once a frame,
`input_pad_step_01972900` (`0x01972900`):

1. **Marks two pad entries**, keyed by the addresses of two globals (`0x059464a8`,
   `0x059566c4`).
2. **Checks whether its owner is ready**: `+0xc4` is 1, `+0xc8` is 0 and `+0xd0` is set.
3. **Advances that owner** with a frame-time descriptor, when it is ready.

While the hunter stands in Iosefka's Clinic the advance branch stays idle.

Scope still to map: pad polling, mapping, dead zones, input buffering.

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

Four small device functions are replaced and edge-verified (Paleblood
[#520](https://github.com/Yharnam-Hunters/Paleblood/pull/520)):

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
them. They are edge-verified: none has been recorded in a game run.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01972900` | 426 | `input_pad_step_01972900` | verified | FD4PadManager map: marks two entries, advances an owner by the frame-time step (patched to 1/15 or 1/240 s; the advance branch is idle in the clinic) |
| `0x02a8bf30` | 108 | `input_mouse_b_open_for_initial_user` | edge-verified | Second mouse device class's open: initial user, close a positive handle at +0x128, sceMouseOpen(user, 0, 0, null); true when the new handle is positive |
| `0x02a8c210` | 164 | `input_mouse_construct` | edge-verified | Mouse device constructor: base ctor with the third argument, vtable 0x056b7d00, clear 0x140..0x177, load sysmodule 0xa9, sceMouseInit, open via slot 32; handle 0 on any failure |
| `0x02a8c420` | 108 | `input_initialize_mouse_for_initial_user` | edge-verified | Initializes or reinitializes mouse input for the initial user, closing a previous connection if necessary. |
| `0x02a9c790` | 84 | `input_pad_set_vibration` | edge-verified | scePadSetVibration on the pad's handle (+0x8) if open (+0xc): large motor from the third argument, small from the second; true on success |

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
- Pad mapping and input buffering have not been studied yet. The sticks' dead zones are applied by
  the game's own code, from the deadzone the controller information reports (only the motion
  sensor's deadband is set through the library).
- Whether the mouse path is used by anything in the shipped game: the mouse device's constructor has two callers (`0x02558eb3`, `0x02558fb6`), not yet mapped, and nothing calls either open directly (only their vtable slots point at them).
- What the shared input device base constructor (`0x02a8d2b0`, six callers) sets up, and which other device classes use it.
