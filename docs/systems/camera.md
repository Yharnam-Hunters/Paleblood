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

The header declares the original entry points. Replacement status is tracked in
[`symbols/functions.csv`](../../symbols/functions.csv); the update remains original while its
dependency contracts and complete behavior are reconstructed.

| Address | Role | Confirmed ABI |
|---|---|---|
| `0x0183ac60` | Follow-camera update | RDI camera, RSI optional subject, RDX optional query world, XMM0.low float time step; no deliberate return value |
| `0x0183fad0` | Pitch getter | RDI camera; float result in XMM0 |
| `0x0183fb20` | Yaw getter | RDI camera; float result in XMM0 |

The update's known caller, `0x018368b0`, calls it at `0x01836c00` and `0x01836e15`, then copies
the camera transform and projection fields. Incoming XMM0 is a scalar time step, even though the
original spills a full SIMD register and the initial decompiler signature omitted it.

The catalog's update size, 20,055 bytes, is not a contiguous body boundary: its endpoint
`0x0183fab7` falls inside an instruction in a reachable cold block. Original-byte control-flow
walking agrees with the native analysis on 3,884 instructions / 20,076 bytes, in inclusive ranges
`0x0183ac60..0x0183f791` and `0x0183f793..0x0183facc`. The single intervening byte and the three
bytes before the next function at `0x0183fad0` are unreachable NOP padding. This establishes
instruction coverage, not semantic correctness or verification.

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
  selector to a byte or short. The update uses selectors zero and one for its paired rotations.

## Patch points

The [community patch XML at commit e344639](https://github.com/shadps4-emu/ps4_cheats/blob/e34463942099072e5d8f5e2bcb3d7d927683b349/PATCHES/Bloodborne.xml)
has two camera edits in `30 FPS++` and
`60FPS++ (no deltatime)`: `0x0183dc96` redirects a jump to include chase-rate adaptation, and
`0x0183dcfe` changes the immediate that sets wall-escape mode when both wing queries miss.

`60 FPS++`, `90 FPS++`, `120 FPS++` and `Uncap FPS++` additionally install twelve camera
trampolines. They adjust parameter/distance interpolation, subject and target chase, direct pitch,
and chase-rate recovery using `SprjFlipper::frame_seconds`. Different sites use a 1/60 or 1/30
time cap, nonlinear rate conversion, complement conversion, or simple time multiplication.
All fourteen in-function edit sites and their cave dependencies need differential coverage;
support cannot be inferred from the jump/flag edits alone.

## Open questions

- Reconstruct the update in readable stages with exact effect order, including cold blocks,
  selective finite-value rollback, consumed request flags and ordered territory traversal.
- Establish complete rotation/matrix helper conventions.
  The sphere-query call uses a float radius, six integer/pointer registers, a stack fraction
  output, optional vector outputs and an AL Boolean result. Its dependency behavior and the
  territory debug-render virtual call still need coverage before replacement.
- Preserve reciprocal-square-root refinements, packed polynomial math, angle reduction,
  conversion/rounding behavior, NaN payloads and scalar operand order. The original Ghidra 12.1.4
  VEX scalar-root rules reversed source roles; an isolated private correction was checked against
  Intel's specification, a synthetic x86-64 run and REA decompilation. Earlier pseudocode must
  not be used as the semantic authority for affected instructions.
- Validate stock behavior and each supported FPS option against original bytes and the actual
  patch, with synthetic edge cases, rejected mutations and independent review. Genuine gameplay
  recordings are still required for full verification; static layout evidence does not replace
  them.
