<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Character

## Summary

Character instances (`ChrIns` in FromSoftware's engine) are updated once a frame through
virtual calls on each instance. `character_frame_update_01cbdb20` (`0x01cbdb20`) is one of
these updates. It:

- fires a pending throw animation event (`W_ThrowAtk` or `W_ThrowDef`) and clears the request;
- regenerates a points stat that floors at -50 and caps at a maximum, which reads like
  stamina. The rate is a parameter byte × 0.01 × a per-character multiplier × a virtual
  factor. Each frame adds the whole part of rate × frame time and keeps the fraction for the
  next frame;
- hands the frame time to the character's modules, copies a few parameters, and builds (then
  drops) a wide string of the character's name with the thread's allocator. Nothing reads the
  string; it looks like the remains of debug code.

In the recordings from Iosefka's Clinic the name string is never built.

Scope still to map: the character instance layout, the modules (`+0x3b0`), the parameters,
the other per-frame updates.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01cbdb20` | 979 | `character_frame_update_01cbdb20` | verified | per-frame character update: throw animation event, points stat regeneration (stamina-like, -50..max), time-step descriptor; the 30/60 FPS patches fix its frame time |
## Structs and globals

- Character instance: frame time `+0xe0`, parameters `+0x58 -> +0x38`, modules `+0x3b0`,
  regeneration fraction `+0x168`, multiplier `+0x16c`, last rate `+0x39c`, name `+0x350`.
- Data module (`modules +0x20`): points `+0x134`, maximum `+0x138`, a no-drop flag at bit 7 of
  `+0x200`.
- `0x0593e88a`: a debug flag (next to the `WorldChrManDbg` singleton `0x0593e880`) that stops
  the stat from dropping.

## Patch points

- The frame time read from `+0xe0` is replaced by a constant: 1/27 s (`30 FPS++`), 1/54 s
  (`60 FPS++`), 1/50 s (`60FPS (no deltatime)`), 1/80 s (`90 FPS++`); `Uncap FPS++` keeps the
  real one. `BB_TARGET_FPS` does the same for 30 and 60. A fixed time slightly longer than one
  frame (1/27 instead of 1/30) speeds regeneration up a little; why the patch authors chose it
  is an open question.

## Open questions

- Is the points stat stamina? A floor below zero would fit a stat that can be overspent.
- Why the patches choose 1/27 and 1/54 rather than 1/30 and 1/60.
