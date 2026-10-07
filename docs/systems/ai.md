<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# AI

## Summary

Enemy and NPC behaviour runs on Havok AI, driven by the game's `SprjHkAiManager` (singleton
`0x059401a0`). Once a frame, `ai_hk_frame_update_0222bc10` (`0x0222bc10`):

- steps the Havok AI world with a fixed time step of 1/30 s;
- with its second flag (always set in the recordings), copies the frame object's matrices to the
  manager's view object and runs the AI debug display and drawing: a debug position (the camera's,
  or the player character's when the character manager exists), debug marks, a draw descriptor over
  every AI object in its groups (or over the whole manager), axis frames looked up in the manager's
  id map, and boxes, after resetting the render manager's debug state to its defaults.

In the recordings from Iosefka's Clinic it resets its three groups, copies the matrices and draws
each AI object; the debug display, axis frames, boxes and the manager-wide draw stay off.

Scope still to map: the AI world step itself (`0x021cdad0`), the groups and objects, the
behaviour of individual enemies.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0222bc10` | 2693 | `ai_hk_frame_update_0222bc10` | verified | SprjHkAiManager per-frame update: steps the AI world by 1/30 s (the 60 FPS patches: 1/60), AI debug display and drawing |
## Structs and globals

- Owner of the update: groups `+0x2bc8` (count) and `+0x2bd0` (`0x20` bytes each: element count `+0x8`,
  elements `+0x10`, 0xb0 bytes each with an object count `+0x58` and object list `+0x60`); debug
  display `+0x2c18`/`+0x2c20`; flags `+0x2c90`..`+0x2c98`; box mode `+0x2c68`; axis-frame pairs
  (id, index) from `+0x2cb8`, `+0x2ec0` of them.
- `SprjHkAiManager`: AI world `+0x10` (its id map at `+0x10`), view object `+0x20`.

## Patch points

- The AI world's time step is 1/30 s; `60 FPS++` (and `90 FPS++`, `60FPS (no deltatime)`) make it
  1/60 s. `BB_TARGET_FPS=60` does the same; `30 FPS++` and `Uncap FPS++` leave it.

## Open questions

- With `Uncap FPS++`, the AI world still steps by 1/30 s each frame: does AI run faster at high
  frame rates?
