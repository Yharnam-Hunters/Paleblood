<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Render

## Summary

`render_frame_update_02377bd0` (`0x02377bd0`) runs once a frame. It walks a list of
reference-counted objects and updates each one; an object whose update says it is finished
loses its reference (destroyed at the last one; an underflow is reported as
"DLReferenceCountObject: Invalid Unref() call") and its node is unlinked and freed. It then
updates a `RendMan`-side child (`RendMan`: singleton `0x05940298`) and steps another child by a
frame time of 1/30 s, or 0 while that child is held.

`render_swap_chain_present` (`0x025b2fb0`, the graphics framework's `GXSwapChainCtrl`) paces
each present: it waits until the requested number of 60 Hz vblanks has passed since the last
one (sleeping until 17 ms before that time, then polling the clock), maps the interval to a
flip mode and presents. In Iosefka's Clinic the game presents with interval 1 and does not wait
here; the frame limiter sets the pace.

`render_yebis_get_recursive_sample_parameters` (`0x00fbc3e0`) belongs to YEBIS, the
post-processing middleware: its `GPUTexUtil_GetRecursiveSampleParameters` works out how many
recursive passes a blur needs, how many samples the last one takes and how far apart the first
one samples, with the middleware's own assertions.

Scope still to map: draw submission, GNM command buffers, shaders, post-processing.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x00fbc3e0` | 670 | `render_yebis_get_recursive_sample_parameters` | verified | YEBIS GPUTexUtil_GetRecursiveSampleParameters: blur pass count, last-pass samples and scale; the FPS patches drop its first assertion |
| `0x02377bd0` | 399 | `render_frame_update_02377bd0` | verified | per-frame: ages a list of ref-counted objects, updates RendMan-side child, steps another child by 1/30 s (Uncap: measured frame time) |
| `0x025b2fb0` | 768 | `render_swap_chain_present` | verified | GXSwapChainCtrl present: waits for the present interval in 60 Hz vblanks, maps it to a flip mode, presents (Uncap: mode 0) |
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
- `Uncap FPS++` presents with flip mode 0 (no vblank sync) at the end of `0x025b2fb0`.
- Every frame-rate patch removes the first assertion of `0x00fbc3e0` (at least 3 base samples):
  at higher frame rates the blur gets fewer samples.

## Open questions

- Everything: this system has not been studied yet.
