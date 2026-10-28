<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Input

The input system handles the controller pads. This page covers the once-a-frame step of FD4PadManager, the manager that keeps a map of pad entries.

**At a glance**

- **Scope:** `input_pad_step_01972900`, the per-frame step of the `FD4PadManager` pad map.
- **Verified:** The step marks two pad entries and, when its owner is ready, advances the owner by a frame-time step.
- **Open:** What the two entries and the owner are; pad polling, mapping, dead zones and input buffering.

## Summary

`FD4PadManager` (singleton `0x058b31f0`) keeps a map of pad entries. Once a frame,
`input_pad_step_01972900` (`0x01972900`):

1. **Marks two pad entries**, keyed by the addresses of two globals (`0x059464a8`,
   `0x059566c4`).
2. **Checks whether its owner is ready**: `+0xc4` is 1, `+0xc8` is 0 and `+0xd0` is set.
3. **Advances that owner** with a frame-time descriptor, when it is ready.

While the hunter stands in Iosefka's Clinic the advance branch stays idle.

Scope still to map: pad polling, mapping, dead zones, input buffering.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01972900` | 426 | `input_pad_step_01972900` | verified | FD4PadManager map: marks two entries, advances an owner by the frame-time step (patched to 1/15 or 1/240 s; the advance branch is idle in the clinic) |

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
- Pad polling, mapping, dead zones and input buffering have not been studied yet.
