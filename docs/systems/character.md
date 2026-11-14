<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Character

Every character instance in the game is updated once a frame. This page covers one of those updates: throw events, the regeneration of a points stat, and the hand-off of frame time to the character's modules.

**At a glance**

- **Scope:** `character_frame_update_01cbdb20`, one of the per-frame updates of a character instance.
- **Verified:** `character_frame_update_01cbdb20` is verified: the throw event, the points regeneration with its fractional carry, and the frame-time hand-off.
- **Open:** Whether the points stat is stamina, whether the update belongs only to the player, and the rest of the instance layout.

## Summary

Character instances (`ChrIns` in FromSoftware's engine) are updated once a frame through
virtual calls on each instance. `character_frame_update_01cbdb20` (`0x01cbdb20`) is one of
these updates.

### What the update does

1. **Fires a pending throw animation event** (`W_ThrowAtk` or `W_ThrowDef`) and clears the
   request.
2. **Regenerates a points stat** that floors at -50 and caps at a maximum (see below). It reads
   like stamina.
3. **Hands the frame time to the character's modules**, copies a few parameters, and builds
   (then drops) a wide string of the character's name with the thread's allocator. Nothing reads
   the string; it looks like the remains of debug code. In the recordings from Iosefka's Clinic
   the name string is never built.

### How the points stat regenerates

The regeneration rate is a product of four terms:

```text
rate = parameter byte × 0.01 × per-character multiplier × virtual factor
```

The multiplier lives at `+0x16c` on the instance, and the last rate is kept at `+0x39c`. Each
frame the update:

1. multiplies the rate by the frame time (read from `+0xe0`);
2. adds the whole part of the result to the points (data module `+0x134`), capped at the
   maximum (`+0x138`);
3. keeps the fraction at `+0x168` and adds it in on the next frame.

Carrying the fraction means a rate too small to add a whole point in one frame still adds up
over several frames instead of being rounded away.

Two switches stop the stat from dropping: bit 7 of the data module's `+0x200`, and the debug
flag `0x0593e88a` next to the `WorldChrManDbg` singleton.

<details>
<summary>Points, stamina and the player</summary>

The source calls this value **points**. Its relationship to stamina, and whether this update
belongs only to the player, are not established. The current evidence-backed account is in
Stats & Progression.

</details>

Scope still to map: the character instance layout, the modules (`+0x3b0`), the parameters,
the other per-frame updates.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01cbdb20` | 979 | `character_frame_update_01cbdb20` | verified | per-frame character update: throw animation event; points stat regeneration (stamina-like; -50..max); time-step descriptor; the 30/60 FPS patches fix its frame time |
| `0x01cd92b0` | 218 | `character_ai_display_update_01cd92b0` | edge-verified | Chr +0x1f8 module: if its ready check (0x01cd9390) passes, gives ai_debug_set_position the two positions of the state module; if it still passes, points its two objects at its markers |
| `0x01e19e20` | 308 | `character_throw_event_01e19e20` | edge-verified | Looks a behavior event up by name and appends {id, 0, 0} to the character graph event ring (grown x2 or to 8 when full); skipped when not ready, unknown or no graph |
| `0x01e4a490` | 375 | `character_state_flags_update_01e4a490` | edge-verified | Chr modules +0x68: folds whether the character is shown into two sticky bytes, copies a param float, sets +0x326 from flags or vtable +0x478, counts the frame, sets the link flag |
| `0x01e59250` | 477 | `character_event_request_update_01e59250` | edge-verified | Chr modules +0x58: fires the pending numbered event (formatted wide string on the thread heap) unless suppressed, clears it, and clears chr +0x20 field +4 for one record id |
| `0x01e5c440` | 282 | `character_counters_refill_01e5c440` | edge-verified | Stepped module: five counters gain a global per-second rate * frame time each (fractions carried, truncation), clamped to 0..their maximum |
| `0x01e88da0` | 242 | `character_gaitem_resolve_handle_01e88da0` | verified | SprjGaitem handle resolve through the manager's index table (slot handle; zero when out of range); reports a missing table / busy / not ready and goes on |
| `0x01e8bae0` | 132 | `character_gaitem_handle_copy_01e8bae0` | edge-verified | SprjGaitem handle copy: one more reference on the live slot it names (bit 23 managed, low 16 bits slot index) |
| `0x01e8bb70` | 132 | `character_gaitem_handle_copy_01e8bb70` | verified | SprjGaitem handle copy, second copy of the same code |
| `0x01e8bc00` | 164 | `character_gaitem_handle_release_01e8bc00` | verified | SprjGaitem handle release: one reference less on its live slot; the last frees the item (0x01e86e60) |
| `0x01e8bcb0` | 202 | `character_gaitem_handle_move_assign_01e8bcb0` | verified | SprjGaitem handle move assignment: releases the old handle, takes the source's, clears the source |

## Structs and globals

**Character instance**

| Offset | Field |
|---|---|
| `+0x58 -> +0x38` | parameters |
| `+0xe0` | frame time |
| `+0x168` | regeneration fraction |
| `+0x16c` | multiplier |
| `+0x350` | name |
| `+0x39c` | last rate |
| `+0x3b0` | modules |

**Data module** (`modules +0x20`): points `+0x134`, maximum `+0x138`, a no-drop flag at bit 7 of
`+0x200`.

**`0x0593e88a`**: a debug flag (next to the `WorldChrManDbg` singleton `0x0593e880`) that stops
the stat from dropping.

## Patch points

The frame-rate patches (see [Frame timing](frame_timing.md)) replace the frame time read from
`+0xe0` with a constant:

| Patch | Frame time used |
|---|---|
| `30 FPS++` | 1/27 s |
| `60 FPS++` | 1/54 s |
| `60FPS (no deltatime)` | 1/50 s |
| `90 FPS++` | 1/80 s |
| `Uncap FPS++` | the real frame time |

`BB_TARGET_FPS` does the same for 30 and 60. A fixed time slightly longer than one frame (1/27
instead of 1/30) speeds regeneration up a little; why the patch authors chose it is an open
question.

## Open questions

- Is the points stat stamina? A floor below zero would fit a stat that can be overspent.
- Why the patches choose 1/27 and 1/54 rather than 1/30 and 1/60.
