<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Event

## Summary

The game's event system: `SprjEmkSystem` (event entries kept in keyed lists) working alongside
`SprjLuaEventMan` (the Lua event scripts) and `SprjEventFlagMan` (event flags).

`event_emk_add` (`0x016efd00`) finds an event entry by id and sub-id in two linked lists, or
allocates, constructs and inserts one in key order, and advances a new entry once by a fixed
1/30 s step. It runs hundreds of times in a minute of gameplay in Iosefka's Clinic.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x016efd00` | 368 | `event_emk_add` | verified | SprjEmkSystem: find or add an event entry in two keyed lists; first advance with the 1/30 s step (1/60 s in the 60 FPS patches) |
## Structs and globals

- Event entry (0xe0 bytes): vtable at `+0x0` (slot `+0x10` advances it by a frame-time
  descriptor), id at `+0x28`, sub-id (16-bit) at `+0x2c`, next entry at `+0x70`.

## Patch points

- `60 FPS++`, `60FPS (no deltatime)` and `90 FPS++` change the first advance's 1/30 s step to
  1/60 s in `0x016efd00`; `BB_TARGET_FPS=60` does the same.

## Open questions

- What the two lists hold (pending and active entries?), and what the ids are.
