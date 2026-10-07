<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Input

## Summary

`FD4PadManager` (singleton `0x058b31f0`) keeps a map of pad entries. Once a frame,
`input_pad_step_01972900` (`0x01972900`) marks two of them, keyed by the addresses of two
globals (`0x059464a8`, `0x059566c4`), and when its owner is ready (`+0xc4` is 1, `+0xc8` is 0,
`+0xd0` set) advances that owner with a frame-time descriptor. While the hunter stands in
Iosefka's Clinic the advance branch stays idle.

Scope still to map: pad polling, mapping, dead zones, input buffering.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01972900` | 426 | `input_pad_step_01972900` | verified | FD4PadManager map: marks two entries, advances an owner by the frame-time step (patched to 1/15 or 1/240 s; the advance branch is idle in the clinic) |
## Structs and globals

- The pad map: an MSVC `std::map` at `FD4PadManager->+0x38->+0x8` (node: left `+0x0`, right
  `+0x10`, nil flag `+0x19`, key `+0x20`, value `+0x28`, a flag at `+0x30` set when the entry is
  used). Keys are compared as signed 64-bit numbers.

## Patch points

- The advance's step is 1/30 s; `30 FPS++` and `60 FPS++` change it to 1/15 s, `Uncap FPS++` to
  1/240 s, `90 FPS++` leaves it. `BB_TARGET_FPS` does the same.

## Open questions

- What the two pad map entries keyed by globals are, and what the owner the step advances is.
- Pad polling, mapping, dead zones and input buffering have not been studied yet.
