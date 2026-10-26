<!-- Reconstruction notes maintained with the implementation and independent analysis. -->

# Camera

## Summary

The character follow camera selects lock-camera parameters, follows the subject's transform,
updates pitch and yaw from control and lock requests, and corrects the camera against the world.
Wall-wing queries and a linked territory list provide additional avoidance. The original debug
controls identify this object as `ChrFollowCam` / `ChrExFollowCam`.

[`game/camera/follow_camera.h`](../../game/camera/follow_camera.h) describes the proven portion
of its layout. Scalar and vector instruction accesses establish the types; debug controls supply
names. Unused debug fields remain padding, and the complete guest class size is unknown.

## Key functions

The header declares the original entry points. The readable update is in
[`game/camera/update.cpp`](../../game/camera/update.cpp), with stages under `game/camera/update/`.
Replacement status is tracked in [`symbols/functions.csv`](../../symbols/functions.csv).
The update includes a synthetic edge generator for stock behavior and the supported FPS
options. See the PR report for current results; genuine gameplay recordings are required
for `verified` status.

| Address | Role | Confirmed ABI |
|---|---|---|
| `0x0183ac60` | Follow-camera update | RDI camera, RSI optional subject, RDX optional query world, XMM0.low float time step; no deliberate return value |
| `0x0183fad0` | Pitch getter | RDI camera; float result in XMM0 |
| `0x0183fb20` | Yaw getter | RDI camera; float result in XMM0 |

The update's known caller, `0x018368b0`, calls it at `0x01836c00` and `0x01836e15`, then copies
the camera transform and projection fields. The public prototype declares a scalar float time
step, but the original spills and later uses all 128 physical bits of XMM0. The public replacement
entry performs a tail jump to a hidden body accepting `engine::Vec4`, preserving the opaque upper
96 bits without a scalar reload or conversion. This machine boundary is specific to x86-64
SysV; it does not assume the upper lanes are dead.

The catalog's former update size, 20,055 bytes, ended inside an instruction in a reachable cold
block. Its corrected size is the 20,076 reachable bytes established by original-byte control-flow
walking and independent analysis: 3,884 instructions, in inclusive ranges
`0x0183ac60..0x0183f791` and `0x0183f793..0x0183facc`. The single intervening byte and the three
bytes before the next function at `0x0183fad0` are unreachable NOP padding. This establishes
instruction coverage; edge verification is a separate execution check.

## Readable update

The stages preserve the original effect order: parameter selection and the null-subject return;
subject compensation, control and fresh transforms; target and angular/input updates; frame and
translation construction; candidate collision, wall-wing escape and chase; final orientation and
collision; finite-value rollback, ordered territory traversal and safe margins. Saved vectors and
object identities remain distinct from fields reloaded after callbacks. The six original vector
math helpers retain their observed calling contracts, including the full-XMM0 quaternion input.

## Structs and globals

- The output transform occupies camera `+0x10..+0x4f`; its forward vector is at `+0x30` and its
  position at `+0x40`. The getters derive pitch/yaw from that forward vector. Pitch retains
  `+0x140` when Y and the computed horizontal length compare equal to zero, including square
  underflow; yaw retains `+0x144` when X and Z are zero. NaNs reach the angle import. `FovY` is
  at `+0x50`.
- A subject transform at `+0x60`, followed target/candidate vectors at `+0xa0..+0x10f`, and
  translation/angular velocities at `+0x110/+0x124` feed the update. Reset/control state begins
  at `+0x130`; camera distance, sphere radius and safety margins are at `+0x184..+0x194`.
  Lock state begins at `+0x250`, wall-escape state at `+0x28c`, and territory avoidance uses
  `+0x2d0/+0x2d1` and a transform copy at `+0x2e0`.
- Parameter selection starts with map ID `+0x32c`. A nonnegative event ID from `+0x324`, or the
  global fallback, overrides it. When boss-lock byte `+0x261` is set, `+0x328` or its fallback can
  override that. Nonnegative `+0x320` has final priority. Both fallback dwords at
  `parameter_overrides` (`0x05956678`) `+0x14/+0x18` are consumed together by an eight-byte store
  of `-1`.
- `SoloParamRepository` (`0x05940340`) exposes availability `+0x9b8` and a holder at `+0x9c0`,
  followed by two `+0x70` links to binary parameter data. Sorted key/index lookup falls back to
  key zero. Header version/flags select 32-bit or 64-bit relative row offsets; the size word
  before the header locates the aligned index. A fixed array layout would be incorrect.
- The parameter interpolation factor is `+0x330`. Proven record inputs are camera distance,
  minimum pitch in degrees, lock pitch shift, subject height offset and vertical FOV in degrees.
  Finite FOV inputs are constrained to 38..48 degrees before conversion to radians; the original
  comparisons preserve NaNs. A null subject still
  permits parameter interpolation and derived speed updates, then returns before most camera
  work and request-flag clearing.
- `wall_camera_mode` (`0x05527a94`) selects ordinary collision correction or wall-wing work.
  `default_control_input` (`0x04b38000`) is used unless camera `+0x334` selects the subject's
  input. `subject_yaw_axis` (`0x0597be80`) is runtime-initialized vector data; its value must not
  be replaced with an assumed unit axis.
- The supplied C library's `_FSin` export dispatches a zero 32-bit integer selector to `sinf`
  and a nonzero selector to `cosf`. Both are tail calls; the wrapper does not narrow the
  selector to a byte or short. The update uses selectors zero and one for its paired rotations,
  and explicitly clears ESI at every call. Preserve that argument even though neither selected
  library body consumes it.

## Original helper call contracts

These are observed SysV64 register and memory contracts for calling the original helpers.
They establish usable bindings without asserting the original C++ source declarations or
replacing the helpers with host math.

| Address | Inputs and outputs |
|---|---|
| `0x00848e80` | RDI aligned 16-byte quaternion output, RSI 16-byte axis input, XMM0.low angle; input read before output store; no deliberate return value |
| `0x01e4d420` | RDI 16-byte quaternion output, full XMM0 by-value rotation vector; returns the output pointer in RAX |
| `0x0083d4d0` | RDI aligned 48-byte position/quaternion/scale input, RSI aligned 64-byte matrix output; the camera uses disjoint storage |
| `0x01c090e0` | RDI world, ESI filter, RDX origin, RCX displacement, XMM0.low radius, R8 optional hit position, R9 optional hit normal, entry `[rsp+8]` optional fraction pointer; returns Boolean in AL |

The sphere query reads complete 16-byte input vectors. A hit writes four bytes through a
non-null fraction pointer and sixteen bytes through each non-null vector output; a miss leaves
the outputs untouched. The camera uses filter `0x25`, no normal output, and six query sites.
The independently assigned float radius leaves the fraction pointer as the seventh GP argument.
The differential harness must capture that overflow slot and permit bounded writes through it.

Territory debug rendering captures the renderer and selected state before calling that state's
vtable slot `+0x28`. The original writes ESI with `0xffffffff`, defining the complete RSI value
as `0x00000000ffffffff`; a full-width wire binding must preserve it. After the call, it reselects
the state from the captured renderer for setting `+0x70` and its dirty flag. Later vector and
settings writes still use the initially captured state. Reloading the manager's renderer or
using one state identity for all these effects would change behavior if the virtual call alters
the selection.

## Patch points

The [community patch XML at commit e344639](https://github.com/shadps4-emu/ps4_cheats/blob/e34463942099072e5d8f5e2bcb3d7d927683b349/PATCHES/Bloodborne.xml)
contains two shared camera edits: `0x0183dc96` redirects the wall-off path into the chase-rate
tail, and `0x0183dcfe` changes the mode stored when both wing queries miss. `BB_TARGET_FPS=30`
reproduces these edits. `BB_TARGET_FPS=60` and `uncapped` additionally reproduce twelve time-scaling
trampolines, covering all fourteen camera edit sites. An unset option selects the stock behavior.
The XML also has 90/120 FPS variants; no additional product options are introduced here.

Each measured-time site reloads the global `SprjFlipper` pointer and its `frame_seconds` member
at `+0x264`, including after callbacks. These unchecked reads remain independent of the incoming
physical XMM0 time register. Sites retain their literal 1/60 or 1/30 caps and operation trees for
rational gain conversion, complement conversion, direct pitch multiplication and rate recovery;
algebraically equivalent reassociation can change floating-point results.

The wall-off redirect has a further effect: although it clears escape mode and skips escape
rotation, the tail tests retained R14b from the wall-global address, whose low byte is `0x94`.
It therefore copies the escape chase rates into the live rates even with wall mode zero.
The replacement preserves that path separately from the both-miss and null-query paths.

## Verification limits

The complete update uses synthetic edge cases for stock, 30, 60 and uncapped behavior.
Authored input graphs and bounded callback aliases exercise specific contracts; their labels
do not establish every gameplay path or callback mutation. Genuine gameplay recordings remain
unavailable, so the update's status is at most `edge-verified`.

Known sensitivity gaps in the original fixed assignments remain explicit: standard rational
substitutions for the blended, distance and complement forms, stock recovery scaling, and
clearing the incoming upper 96 XMM0 bits were not distinguished. These observations do not
establish equivalence or dead lanes. The source retains the instruction-derived formulas and
physical-register bridge. The front-chase selector fixture uses zero chase gain; it does not
establish sensitivity to a nonzero gain. Current run results and mutation evidence belong in
the PR report.

Reciprocal-square-root refinements, packed polynomial math, angle reduction, conversions and
operand order remain explicit. The original Ghidra 12.1.4 VEX scalar-root rules reversed source
roles; an isolated correction was checked against Intel's specification, a synthetic x86-64 run
and REA decompilation. Original instructions remain the authority for affected operations.
