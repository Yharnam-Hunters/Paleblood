<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Physics

The game's rigid bodies live in one physics world: shared tables of bodies, motions and constraints that other objects index into, and listeners that hear when a body changes.

**At a glance**

- **Scope:** The physics world reached through the physics manager, and the objects that own bodies in it.
- **Verified:** The physics-instance constructor `physics_instance_construct_01c0c2b0` (`0x01c0c2b0`).
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

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01c0c2b0` | 3222 | `physics_instance_construct_01c0c2b0` | verified | physics instance constructor over a body set: back-links, shape copy, controller (step 1/30 s; 60 FPS patches 1/60), motion rescale, listeners |

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

- What kind of object is a physics instance: ragdolls, breakables, or both?
- What do the physics manager's two special modes mean? Both are on while the clinic loads.
