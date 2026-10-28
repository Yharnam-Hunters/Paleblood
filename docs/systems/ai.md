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
| `0x0222bc10` | 2693 | `ai_hk_frame_update_0222bc10` | verified | SprjHkAiManager per-frame update: steps the AI world by 1/30 s (the 60 FPS patches: 1/60), AI debug display and drawing |

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
