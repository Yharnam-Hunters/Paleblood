<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Camera

The follow camera: how it turns the stick, the character's movement and a lock-on target into a camera position each frame, how it keeps out of walls, and where its field of view comes from.

**At a glance**

- **Scope:** Camera update, targeting and lock-on, collision with the world, field of view.
- **Verified:** The whole follow-camera update, `camera_update_follow` (20 KB), and the pitch and yaw getters are replaced and edge-verified, each with an independent review.
- **Our option:** `BB_TARGET_FPS` (30, 60, uncapped) reproduces what the frame-rate patches change in the update.
- **Open:** Who calls the update and sets its requests, the parameter table's other fields, and what the camera looks like in the running game.

## Summary

### What the follow camera does each frame

`camera_update_follow` (`0x0183ac60`) takes the camera, the character it follows (the
*subject*), a world to cast spheres against, and the frame's time step. It works in stages:

1. **Parameters.** It picks a row from the lock-camera parameter table and eases the camera
   toward it: distance, lowest pitch, lock-on pitch shift, height above the subject and vertical
   field of view. The row's key is the map's ID, replaced in turn by an event ID, a boss ID while a
   boss lock is requested, and an explicit override, whichever are set; failing that, key 0.
   It also sets rotation speeds from an option rate (squared, between a minimum and a maximum).
   With no subject the update stops here.
2. **Carry-along.** It moves last frame's camera and subject points by a stored velocity and
   turns them by a stored angular velocity around a pivot that it reaches through the subject.
3. **Stick.** It reads a stick vector from the subject, or a default one. Stick input marks the
   camera as hand-controlled. Once the stick is released, a wait and then a release time hand
   control back.
4. **Target.** It chases the subject's position and places the target point at the subject plus
   the parameter row's offset. The camera's pivot (*fulcrum*) and its first position guess (the
   *candidate*) move with the subject.
5. **Angles.** Exactly one of these sets pitch and yaw:
   - **reset:** stored reset angles, or level pitch and the subject's heading;
   - **lock-on:** aim at the lock target (below);
   - **front lock:** turn smoothly to the subject's heading for up to a set time. It ends when the
     time runs out, when the angles come within a small tolerance, or on stick input. (The name
     is ours; we take it to be "reset the camera behind the character".)
   - **free:** stick input times the rotation speed. Pitch stays between a minimum and a maximum.
     Pushing the stick hard up or down past a set angle ramps into a faster rotation that also
     moves pitch directly toward its limit.

   Afterwards, unless the camera is locked on or in front lock, pitch drifts back toward a
   target pitch.
6. **Placement and collision.** It builds the look direction from the angles and keeps the
   fulcrum at its distance from the target. Sideways movement of the subject shortens that
   distance, between a minimum and a maximum rate. Then it avoids walls in one of two ways (see
   *Keeping out of walls*).
7. **Chase.** The final position eases toward the candidate. Up and down chase at one rate, the
   ground plane at another, and both depend on how far the direction has swung: below a minimum
   angle only the height follows, above a maximum angle both follow fully. Right after stick
   rotation the camera follows at once.
8. **Orientation and checks.** It builds the camera's axes and casts a sphere from the target to
   the final position. It clears the one-frame requests (reset, lock, boss lock, front lock). If
   any axis or the position is not a number, it restores the whole previous frame. Then it pushes
   the camera out of *territories* (below) and pulls it a safety margin toward the target.

### Targeting and lock-on

While a lock is requested, the camera aims from its orbit point at a stored lock-target point.
The pitch is raised by a shift that grows with the field of view and the row's lock-pitch-shift
ratio, plus an angle from the distances involved. The pitch limits blend from the lock-on limits
to the normal limits as the height difference to the target grows. The angles chase the goal at a
lock rotation rate. Inside a dead angle the camera does not move. If lock adjustment is
enabled, the stick nudges pitch and yaw up to a set maximum. The update does not choose the
target: it only reads the point and the request flags.

### Keeping out of walls

A global byte (`0x05527a94`, which we call the wall camera mode) picks one of two methods:

- **Zero: pull in.** A sphere is cast from the target to the fulcrum, then to the candidate. On a
  hit the point moves to where the sphere stopped, but no closer than a minimum.
- **Non-zero: swing away.** Two *wing* spheres are cast at a set angle to each side of the
  camera's line to the target. If exactly one hits, an escape yaw builds up and turns the camera
  away from that side. It builds at a strong rate while the hit is close, easing toward a weak
  rate as the hit lies farther along the probe. The swing is speed-limited, fades when nothing
  new is hit, and switches the chase rates to escape rates, which ease back afterwards. Stick input, lock-on, front lock and reset
  switch the swing off.

In both modes the final position gets one more sphere cast (step 8).

**Territories** are spheres in a list the update reaches through a global (`0x0593e858`). Each
has a centre and a one-byte radius. If the camera is inside an enabled one, it moves back along
its view axis to the sphere's surface, then a sphere cast checks the new position. A debug flag
draws them.

### Field of view

The vertical field of view (`+0x50`, radians) eases toward the parameter row's value in degrees.
Below a minimum the row's value is replaced by a fixed minimum. Above a maximum it is capped at
48°. A flag raises the target point toward the height that half the view angle covers at the
current distance, going halfway to it, so the subject stays in frame.

### Which way the follow camera looks

The follow camera keeps its orientation as a direction it is looking along. Two small functions
turn that into angles:

- `camera_follow_pitch` (`0x0183fad0`) gives the up-down angle, from the direction's height
  against its length across the ground (an `atan2`), negated;
- `camera_follow_yaw` (`0x0183fb20`) gives the left-right angle, from the direction's two ground
  components.

When the direction has no length to measure (both components zero), each falls back to the angle
it stored last time instead of computing a meaningless one.

<details>
<summary>How these were verified</summary>

- `camera_update_follow`: by [@evill33t](https://github.com/evill33t) in #112, merged in
  [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489) (commit `9786f4b9`). 72
  constructed edge cases pass against the original on stock code and under each
  `BB_TARGET_FPS` option (30, 60, uncapped) against the matching patch: 72/72 in all four
  runs. The cases cover both wall modes, the reset, lock and front-lock flags, chase limits,
  the safety margin, parameter selection with no subject, territories and their debug
  drawing, frame-time edge values, sphere-cast hits and misses, and callbacks that change the
  camera mid-update. An independent review rebuilt the change, reran every case and checked
  that deliberately planted mistakes are caught. It approved the change.
- `camera_follow_pitch` and `camera_follow_yaw`: by [@evill33t](https://github.com/evill33t)
  in #109, merged in [#280](https://github.com/Yharnam-Hunters/Paleblood/pull/280). 32 and 30
  edge cases pass, including not-a-number directions, and an independent review approved
  them.
- All three are edge-verified only. No camera behaviour has been recorded in the running game.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0183ac60` | 20077 | `camera_update_follow` | edge-verified | Updates subject-relative camera position and orientation through input, chase, collision avoidance and territory constraints; applies selected frame-rate gains |
| `0x0183fad0` | 79 | `camera_follow_pitch` | edge-verified | Pitch from the forward basis; preserve scalar square underflow and zero fallback; call atan2 and XOR the returned sign bits |
| `0x0183fb20` | 43 | `camera_follow_yaw` | edge-verified | Yaw from the forward X/Z direction; exact signed-zero fallback preserves stored angle; unordered components reach atan2 unchanged |

## Structs and globals

**The follow camera** (partial; the full size is unknown). The field names describe what the
update does with each field; they are ours, not the game's.

| Offset | Fields |
|---|---|
| `+0x10`, `+0x50`, `+0x60` | camera transform (axes and position); vertical field of view (radians); subject transform |
| `+0xa0`–`+0x110` | points: target reference, target, cast origin, orbit origin, fulcrum, candidate, chased position; velocity |
| `+0x13c`–`+0x15c` | reset requests; pitch and yaw (`+0x140`, read by the getters); target pitch; hand control |
| `+0x180`–`+0x234` | distances, sphere radius, safety margin; chase rates and angles (normal, escape, lock-on); pitch limits; rotation speeds |
| `+0x240`–`+0x288` | lock-on: rotation rate, dead angle, pitch shift, target point (`+0x250`), requests (`+0x260`–`+0x262`), front-lock timer and angles, stick adjustment |
| `+0x28c`–`+0x2e0` | wall escape (mode, yaw, rates, wing shape, hit fractions); territory flags and working transform |
| `+0x320`–`+0x344` | parameter IDs (override, event, boss lock, map) and chase rate; stick source; raise target with the field of view |

**Lock-camera parameter row:** camera distance (`+0x0`), lowest pitch in degrees (`+0x4`),
lock-pitch shift ratio (`+0x8`), height above the subject (`+0xc`), vertical field of view in
degrees (`+0x14`). The table hangs off the solo parameter repository (`0x05940340`), at `+0x9c0`
(present when `+0x9b8` is non-zero). The update reads three of its storage formats.

**Globals:** one-shot event and boss-lock IDs (`0x05956678`, `+0x14` and `+0x18`), cleared every
update; the wall camera mode (`0x05527a94`); the territory list (`0x0593e858`); the default stick
vector (`0x04b38000`). The world sphere cast is `0x01c090e0`.

## Patch points

The frame-rate patches edit `camera_update_follow`. While our replacement is installed, its
original bytes never run, so a patch line inside it does nothing. `BB_TARGET_FPS` rebuilds the
same changes in code (see [Frame timing](frame_timing.md)):

| Where | Original | `30 FPS++` / option 30 | `60 FPS++`, `Uncap FPS++` / options 60, uncapped |
|---|---|---|---|
| 12 sites (`0x0183ae75`, `0x0183af8c`, `0x0183ba97`, `0x0183bc9b`, `0x0183c30f`, `0x0183c784`, `0x0183cef0`, `0x0183e2b2`, `0x0183e7da`, `0x0183e964`, `0x0183f97e`, `0x0183fa9f`) | fixed per-frame rates (parameters, distance, subject and target chase, lock and front-lock rotation, target-pitch drift, direct pitch, escape easing) | unchanged | each rate is rescaled from the measured frame time, capped at 1/60 s (1/30 s for parameter and escape easing) |
| `0x0183dc96`, `0x0183dcfe` | when both wings miss, the escape mode becomes 1; with the wall mode off, only the escape mode is cleared | mode 0; with the wall mode off, the chase rates switch to the escape rates | same |
| `0x04d25e94` (constant) | escape easing step | 1/15 | 1/30 |
| `0x04d25e68` (constant) | camera-distance easing rate | 0.1 | 0.1 |

The two constants lie outside the function, so those patch lines still apply with the replacement
installed: the option runs together with the patch, which supplies them, as it does for the
other functions the option covers.

`90 FPS++` and `120 FPS++` edit the same sites; we have no option for them.

Three other kinds of patch edit the function too, and also do nothing while it is replaced; none
has a matching option. **Disable Camera Auto Rotation via Movement** blanks four instructions
(`0x0183c6e8`, `0x0183c870`, `0x0183c984`, `0x0183dde6`). **Increased camera distance**,
**Smaller Hunter** and **Smallest Hunter** jump out at `0x0183ae68` (the last two also at
`0x0183aeef`); the first appears to scale the target camera distance by 1.5.

## Open questions

- Who calls `camera_update_follow`, and who sets the lock target, the lock and front-lock
  requests and the wall camera mode? Does the front lock really match the "camera behind the
  character" button? Is the wall mode a menu option?
- The other fields of the parameter row (`+0x10` is skipped) and the parameter file's identity.
- What the carry-along pivot is (a moving platform or a mount, for example). Its velocity fields
  are set elsewhere.
- Which of the 12 frame-rate sites carries which rate: the table matches them by count, not site
  by site. Which stage the auto-rotation patch's four sites belong to.
- `0x0183a0e0`, just before the update, is edited by the distance, Hunter-size and frame-rate
  patches. It is not mapped.
- What the camera does in the running game: nothing has been recorded beyond constructed cases.
