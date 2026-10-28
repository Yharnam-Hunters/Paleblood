<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Event

The event system keeps the game's event entries, runs its Lua event scripts and stores its event flags. This page covers how an event entry is found or added, and how one event flag bit is written.

**At a glance**

- **Scope:** `SprjEmkSystem` (event entries), `SprjLuaEventMan` (Lua event scripts) and `SprjEventFlagMan` (event flags).
- **Verified:** `event_emk_add` is verified; `event_flag_write` is edge-verified against constructed cases.
- **Open:** What the two entry lists hold, what the ids mean, and what any real flag ID means in play.

## Summary

The game's event system is three managers working together:

| Manager | What it keeps |
|---|---|
| `SprjEmkSystem` | event entries, kept in keyed lists |
| `SprjLuaEventMan` | the Lua event scripts |
| `SprjEventFlagMan` | event flags |

### Adding an event entry

`event_emk_add` (`0x016efd00`) is the way into the entry lists:

1. It looks for an event entry by id and sub-id in two linked lists.
2. If there is none, it allocates, constructs and inserts one in key order.
3. It advances a new entry once by a fixed 1/30 s step.

It runs hundreds of times in a minute of gameplay in Iosefka's Clinic.

### Writing an event flag

`event_flag_write` (`0x017cfcc0`) changes one stored bit. Flags are stored in groups, so the
function first works out which group holds the flag, then which bit in that group:

1. It divides the flag ID by the store's configured flags-per-group value.
2. It finds an exact matching group in a keyed tree.
3. It selects either a direct storage pointer or a stride-based offset into shared storage.
4. It sets or clears the bit. Within a byte, flags are numbered from the most significant bit; a
   nonzero `enabled` argument sets the selected bit and zero clears it while retaining its
   neighbors.

A missing group, unsupported storage kind, or null storage pointer leaves memory unchanged.

This replacement is **edge-verified**, not fully verified: it matches the original on
constructed edge cases, not on recorded gameplay.

<details>
<summary>How the flag write was verified</summary>

The merged
[source and layout](https://github.com/Yharnam-Hunters/Paleblood/blob/af97b90bb1e88853adfc04b8da2b3d7506516565/game/event/flag_write.cpp)
and [verification record](https://github.com/Yharnam-Hunters/Paleblood/blob/af97b90bb1e88853adfc04b8da2b3d7506516565/symbols/verification.csv)
report matching original-instruction results on 14 constructed edge fixtures. Those cover tree
search paths, byte boundaries, set/clear operations, missing groups, and storage selection.

</details>

<details>
<summary>What the fixtures do not establish</summary>

They do not identify a real flag ID's gameplay meaning, establish a zero group-size contract, or
replace recorded gameplay cases. No fixture was rerun for this article.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x016efd00` | 368 | `event_emk_add` | verified | SprjEmkSystem: find or add an event entry in two keyed lists; first advance with the 1/30 s step (1/60 s in the 60 FPS patches) |
| `0x017cfcc0` | 177 | `event_flag_write` | edge-verified | finds an exact event-flag group in a tree and sets or clears one bit |

## Structs and globals

**Event entry** (0xe0 bytes):

| Offset | Field |
|---|---|
| `+0x0` | vtable; slot `+0x10` advances the entry by a frame-time descriptor |
| `+0x28` | id |
| `+0x2c` | sub-id (16-bit) |
| `+0x70` | next entry |

## Patch points

- `60 FPS++`, `60FPS (no deltatime)` and `90 FPS++` change the first advance's 1/30 s step to
  1/60 s in `0x016efd00`; `BB_TARGET_FPS=60` does the same. The patches are described in
  [Frame timing](frame_timing.md).

## Open questions

- What the two lists hold (pending and active entries?), and what the ids are.
