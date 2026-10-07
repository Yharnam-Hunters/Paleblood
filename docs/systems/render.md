<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Render

## Summary

`render_frame_update_02377bd0` (`0x02377bd0`) runs once a frame. It walks a list of
reference-counted objects and updates each one; an object whose update says it is finished
loses its reference (destroyed at the last one; an underflow is reported as
"DLReferenceCountObject: Invalid Unref() call") and its node is unlinked and freed. It then
updates a `RendMan`-side child (`RendMan`: singleton `0x05940298`) and steps another child by a
frame time of 1/30 s, or 0 while that child is held.

Scope still to map: draw submission, GNM command buffers, shaders, post-processing.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x02377bd0` | 399 | `render_frame_update_02377bd0` | verified | per-frame: ages a list of ref-counted objects, updates RendMan-side child, steps another child by 1/30 s (Uncap: measured frame time) |
## Structs and globals

- The update's owner: list sentinel `+0x10` (next `+0x0`, prev `+0x8`, object `+0x10`), count
  `+0x18`, allocator `+0x20` (frees nodes through vtable slot `+0x70`), children at `+0x70`
  (stepped through vtable slot `+0x40`) and `+0x80`, the held flag `+0x8c`, a byte `+0x98` copied
  into the `+0x70` child each frame.
- Reference-counted objects: vtable slot 0 destroys, count at `+0x8` (atomic).

## Patch points

- `Uncap FPS++` makes the `+0x70` child's step the flipper's measured frame time (`+0x264`)
  whatever the held flag says, through a small code cave inside the function, and drops the
  `RendMan` check. The 1/30 s it reads otherwise is the shared constant at `0x04d29170`, which
  the 60, 90 and uncapped patches rewrite in the data segment.

## Open questions

- Everything: this system has not been studied yet.
