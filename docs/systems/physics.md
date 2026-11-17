<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Physics

The game's rigid bodies live in one physics world: shared tables of bodies, motions and constraints that other objects index into, and listeners that hear when a body changes. The constructor that builds a physics instance is verified, and the nine helpers it calls are now replaced too.

**At a glance**

- **Scope:** The physics world reached through the physics manager, and the objects that own bodies in it.
- **Verified:** The physics-instance constructor `physics_instance_construct_01c0c2b0` (`0x01c0c2b0`).
- **Edge-verified:** All nine helpers the constructor calls: the body set, controller, tracker and shape constructors, the shape bounds, the frame-to-transform step, the root body's ragdoll update and the id-vector growth.
- **Fixed step:** An instance's controller steps at 1/30 s; the 60 FPS patches and `BB_TARGET_FPS=60` make it 1/60 s.
- **Open:** What kind of object a physics instance is, and what the manager's two special modes mean.

## Summary

### The physics world

The game keeps its rigid bodies in a physics world reached through the physics manager
(`0x0593d700` → `+0x28` → `+0x8`). The world holds tables that everything else indexes into:

| Table | Offset in the world | Entry size (bytes) |
|---|---|---|
| Bodies | `+0x20` | `0x90` |
| Motions | `+0xe0` | `0x80` |
| Constraints | `+0x128` | `0x38` |
| Listeners (told when a body changes) | `+0x538` | list |

### How a physics instance is built

`physics_instance_construct_01c0c2b0` (`0x01c0c2b0`) is the constructor of a *physics instance*:
an object that owns a set of bodies in that world (a ragdoll or a breakable object would look like
this; which one is still open). Reading it is a good lesson in how a constructor wires a new object
into shared tables:

1. It asks the body-set constructor for its bodies, then moves every **active** body (flags
   `+0x40 & 3`) by the offset it was given.
2. It writes a 16-byte **back-link** for each of its bodies and constraints, and points the world's
   tables at them, so the world can find the instance from any body.
3. It copies the set's shape table (`0x30` bytes per shape) into a buffer from a dedicated heap,
   handing it to a shape object, and builds a **controller** whose fixed frame step is 1/30 s and
   whose transform starts at the root body.
4. Unless the physics manager is in one of its two special modes (as it is while Iosefka's Clinic
   loads), it rescales each active body's four 16-bit motion values and tells the world's listeners
   about each body, removing listeners that are flagged for removal.
5. Last, it creates its own listener and a tracker, and collects the distinct motion types above 4
   of its bodies.

### How the motion values are rescaled

The four values are stored as the *top halves* of 32-bit floats (the format known as bfloat16):
16 bits holding a float's sign, exponent and the top of its mantissa. Step 4 above works like
this:

1. **Widen.** Each value goes back to a float by putting it in the upper half of a float's bit
   pattern.
2. **Scale.** The first three are multiplied by 0.2, and all four by 1.00390625.
3. **Narrow.** Only the top 16 bits of each result are kept again (an arithmetic shift of the
   bit pattern, then a saturating pack).

It is a compact way to store floats at half the size, and an example of why a replacement has to
reproduce the exact instructions: the truncation works on the float's bits, not its value.

### The constructor's helpers

The constructor was verified while it still called nine helpers as original code. All nine are now
replaced and edge-verified, by [@FFwLo](https://github.com/FFwLo). Many of the objects are Havok's
(the physics middleware the game uses), with its reference counting and growable `hkArray` lists.

- **The body set** (`0x00ae8700`) is built from a source description: it keeps a reference to the
  source's shape set and copies its list of 32-bit body indices into a buffer from the shape heap.
  Havok only counts references on objects that record a size, and the count wraps within 16 bits.
- **The controller** (`0x00ae3fc0`) is built over the body set: it empties its lists, runs its
  set-up with the set and the world, and appends the caller's 0x30-byte parameter block.
- **Growing an id list** (`0x02bc1ca0`) moves a vector of 32-bit ids into a larger buffer from its
  heap and frees the old one; many systems use it, not only physics. A failed allocation goes
  through the engine's report and then carries on with no buffer, as the original does
  ([#930](https://github.com/Yharnam-Hunters/Paleblood/pull/930) for these three).
- **The tracker** (`0x00ae6d00`, with its set-up cleared by `0x00ae6dc0`) is a Havok object that
  keeps one 32-bit entry per shape flag, each starting at 1.
- **The shape object** (`0x00ac46a0`) keeps the shape set, empties its four lists and goes on to
  fill them; **the shape summary** (`0x00ac5e00`) works out the smallest box around each shape
  entry's first row. With no entries the box stays inside out (minimum at the largest float,
  maximum at its negative), and a not-a-number value in an entry wins, as in the original.
- **The transform from a frame** (`0x0083d6d0`) turns three axes and a position into a transform:
  a rotation built from the axes, the position, and a scale of 1.
- **The root update** (`0x00ae6180`) is the ragdoll update of the root body. The constructor calls
  it last, and two other systems call it too: it builds a pose, applies it with a weight, and
  frees it ([#796](https://github.com/Yharnam-Hunters/Paleblood/pull/796) for these six).

<details>
<summary>How the helpers were verified</summary>

Each helper was compared with the original on constructed cases in pattern-filled buffers, so
every byte a function writes or leaves alone is checked, with the functions it calls stubbed
and their arguments recorded: 3 to 16 cases each
([#796](https://github.com/Yharnam-Hunters/Paleblood/pull/796),
[#930](https://github.com/Yharnam-Hunters/Paleblood/pull/930)). The id-list growth also
passes its 11 cases recorded from a boot; the others are not reached in the boot recordings.
All nine are edge-verified, not verified.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0083d6d0` | 54 | `physics_transform_from_frame_0083d6d0` | edge-verified | Frame (three axes, position) to a transform: rotation by 0x00849900 from the axes at +0x10, the position at +0x0, a scale of 1.0 at +0x20 |
| `0x00ac46a0` | 96 | `physics_shape_construct_00ac46a0` | edge-verified | Shape object: keeps the shape set at +0x0, empties the four hkArrays (+0x8, +0x18, +0x28, +0x40), then tail-jumps into the fill at 0x00ac4700 |
| `0x00ac5e00` | 124 | `physics_shape_summary_00ac5e00` | edge-verified | Lane-wise min/max of each shape entry first row (count from the set at +0x30, read before the refresh 0x00ac4f20), starting from FLT_MAX and -FLT_MAX |
| `0x00ae3fc0` | 280 | `physics_controller_construct_00ae3fc0` | edge-verified | Controller: base constructor, vtable, three empty hkArrays (+0x40, +0xc0, +0xd0), +0x68/+0x70 cleared, setup 0x00ae40e0; the 0x30-byte parameter block appended (reserved on the shape heap if needed) |
| `0x00ae6180` | 110 | `physics_root_update_00ae6180` | edge-verified | Ragdoll update of a root body: a pose built on the stack (0x00ae6620), applied (0x00ae61f0) with the float weight in all four lanes, then freed (0x00ae6510) |
| `0x00ae6d00` | 177 | `physics_tracker_construct_00ae6d00` | edge-verified | Tracker: Havok header and vtable, keeps the setup shape set and pair, one 32-bit entry per shape flag set to 1 (reserved on the shape heap), byte +0x38 cleared |
| `0x00ae6dc0` | 16 | `physics_tracker_setup_defaults_00ae6dc0` | edge-verified | Clears a tracker setup: its pair (+0x8) and shape set (+0x0) |
| `0x00ae8700` | 414 | `physics_body_set_construct_00ae8700` | edge-verified | Body set: base constructor 0x00f359c0 with all seven arguments, vtable, a reference to the source shape set (+0x78), the source 32-bit indices (+0x80) copied into a shape heap buffer |
| `0x01c0c2b0` | 3222 | `physics_instance_construct_01c0c2b0` | verified | physics instance constructor over a body set: back-links; shape copy; controller (step 1/30 s; 60 FPS patches 1/60); motion rescale; listeners |
| `0x02bc1ca0` | 218 | `physics_ids_reserve_02bc1ca0` | edge-verified | Grow a 32-bit id vector to a capacity: 4-aligned buffer from its heap (+0x20), ids copied, old buffer freed; a null buffer goes through the transaction failure report and is kept |

## Structs and globals

| Object | Field | Meaning |
|---|---|---|
| Physics manager (`0x0593d700`) | `+0x28` → `+0x8` | The world |
| | `+0x274`, `+0x275` | Special-mode flags |
| World | `+0x20` | Body table |
| | `+0xe0` | Motion table |
| | `+0x128` | Constraint table |
| | `+0x538` | Listener list (a tagged pointer: the low two bits are flags) |
| Body (`0x90` bytes) | `+0x30` | Position |
| | `+0x40` | Flags |
| | `+0x68` | Motion index |
| | `+0x88` | Back-link |
| Instance | `+0x38` | Body set |
| | `+0x70` / `+0x78` | Back-links |
| | `+0x88` | Shape object |
| | `+0x90` | Controller (frame step at `+0x60`) |
| | `+0x128` | Own listener |

## Patch points

- The controller's frame step is 1/30 s; `60 FPS++` (and `90 FPS++`, `60FPS (no deltatime)`) make it
  1/60 s. `BB_TARGET_FPS=60` does the same.

## Open questions

- What kind of object is a physics instance: ragdolls, breakables, or both? The root body's
  ragdoll update in its last step suggests ragdolls at least, but that is not confirmed.
- What do the physics manager's two special modes mean? Both are on while the clinic loads.
