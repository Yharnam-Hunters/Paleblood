<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Input

The input system handles the controller pads. This page covers the once-a-frame step of FD4PadManager, the manager that keeps a map of pad entries.

**At a glance**

- **Scope:** `input_pad_step_01972900`, the per-frame step of the `FD4PadManager` pad map.
- **Verified:** The step marks two pad entries and, when its owner is ready, advances the owner by a frame-time step.
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
| `libSceMouse` | `sceMouseInit`, `sceMouseOpen`, `sceMouseClose`, `sceMouseRead` | A mouse path exists, set up by three small functions around `0x02a8c000`. |
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

Functions already found, still unnamed (open to claim):

| Address | Size | Library calls | Likely role |
|---|---|---|---|
| `0x02a9c790` | 84 | `scePadSetVibration` | sets a pad's vibration |
| `0x02a8bf30` | 108 | `sceMouseOpen`, `sceMouseClose`, `sceUserServiceGetInitialUser` | mouse open or reopen |
| `0x02a8c210` | 164 | `sceMouseInit`, `sceSysmoduleLoadModule`; calls `0x02a8c420`, `0x02a8d2b0` | mouse setup: loads the module, initialises, opens |
| `0x02a8c420` | 108 | `sceMouseOpen`, `sceMouseClose`, `sceUserServiceGetInitialUser` | mouse open or reopen (a twin of `0x02a8bf30`) |

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01972900` | 426 | `input_pad_step_01972900` | verified | FD4PadManager map: marks two entries, advances an owner by the frame-time step (patched to 1/15 or 1/240 s; the advance branch is idle in the clinic) |
| `0x02a8c420` | 108 | `input_initialize_mouse_for_initial_user` | original | Initializes or reinitializes mouse input for the initial user, closing a previous connection if necessary. |

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
- Whether the mouse path is used by anything in the shipped game.
