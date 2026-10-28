<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Render

The render system draws each frame and presents it to the screen. This page covers three mapped pieces: the per-frame render update, the pacing of each present, and one helper from the YEBIS post-processing middleware.

**At a glance**

- **Scope:** `render_frame_update_02377bd0`, `render_swap_chain_present` and `render_yebis_get_recursive_sample_parameters`.
- **Verified:** All three: the reference-counted object list, the vblank pacing of the present, and the blur sample counts.
- **Open:** What the listed objects and the two children are; draw submission, GNM command buffers, shaders, post-processing.

## Summary

### The per-frame update

`render_frame_update_02377bd0` (`0x02377bd0`) runs once a frame:

1. It walks a list of reference-counted objects and updates each one.
2. An object whose update says it is finished loses its reference (destroyed at the last one; an
   underflow is reported as "DLReferenceCountObject: Invalid Unref() call"), and its node is
   unlinked and freed.
3. It updates a `RendMan`-side child (`RendMan`: singleton `0x05940298`).
4. It steps another child by a frame time of 1/30 s, or 0 while that child is held.

### Pacing the present

`render_swap_chain_present` (`0x025b2fb0`, the graphics framework's `GXSwapChainCtrl`) paces
each present:

1. It waits until the requested number of 60 Hz vblanks has passed since the last present. It
   sleeps until 17 ms before that time, then polls the clock.
2. It maps the interval to a flip mode.
3. It presents.

In Iosefka's Clinic the game presents with interval 1 and does not wait here; the frame limiter
sets the pace.

### Blur sampling (YEBIS)

`render_yebis_get_recursive_sample_parameters` (`0x00fbc3e0`) belongs to YEBIS, the
post-processing middleware. Its `GPUTexUtil_GetRecursiveSampleParameters` works out:

- how many recursive passes a blur needs;
- how many samples the last one takes;
- how far apart the first one samples.

It carries the middleware's own assertions.

Scope still to map: draw submission, GNM command buffers, shaders, post-processing.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x00fbc3e0` | 670 | `render_yebis_get_recursive_sample_parameters` | verified | YEBIS GPUTexUtil_GetRecursiveSampleParameters: blur pass count, last-pass samples and scale; the FPS patches drop its first assertion |
| `0x02377bd0` | 399 | `render_frame_update_02377bd0` | verified | per-frame: ages a list of ref-counted objects, updates RendMan-side child, steps another child by 1/30 s (Uncap: measured frame time) |
| `0x025b2fb0` | 768 | `render_swap_chain_present` | verified | GXSwapChainCtrl present: waits for the present interval in 60 Hz vblanks, maps it to a flip mode, presents (Uncap: mode 0) |

## Structs and globals

**The update's owner**

| Offset | Field |
|---|---|
| `+0x10` | list sentinel (next `+0x0`, prev `+0x8`, object `+0x10`) |
| `+0x18` | count |
| `+0x20` | allocator (frees nodes through vtable slot `+0x70`) |
| `+0x70` | child, stepped through vtable slot `+0x40` |
| `+0x80` | child |
| `+0x8c` | the held flag |
| `+0x98` | a byte copied into the `+0x70` child each frame |

**Reference-counted objects**: vtable slot 0 destroys, count at `+0x8` (atomic).

## Patch points

The frame-rate patches are described in [Frame timing](frame_timing.md).

- `Uncap FPS++` makes the `+0x70` child's step the flipper's measured frame time (`+0x264`)
  whatever the held flag says, through a small code cave inside the function, and drops the
  `RendMan` check. The 1/30 s it reads otherwise is the shared constant at `0x04d29170`, which
  the 60, 90 and uncapped patches rewrite in the data segment.
- `Uncap FPS++` presents with flip mode 0 (no vblank sync) at the end of `0x025b2fb0`.
- Every frame-rate patch removes the first assertion of `0x00fbc3e0` (at least 3 base samples):
  at higher frame rates the blur gets fewer samples.

## Open questions

- What the reference-counted objects in the per-frame list are, and what the two children are.
- Draw submission, GNM command buffers, shaders and post-processing have not been studied yet.
