<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Camera

The camera system covers the camera update, targeting, collision with the world and field of view. The first two follow-camera functions are mapped.

**At a glance**

- **Scope:** Camera update, targeting, collision with the world, field of view.
- **Verified:** `camera_follow_pitch` and `camera_follow_yaw` are edge-verified against constructed cases.
- **Open:** The camera update itself, targeting, collision and field of view.

## Summary

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

By [@evill33t](https://github.com/evill33t) in #109, merged in [#280](https://github.com/Yharnam-Hunters/Paleblood/pull/280): 32 and
30 constructed edge cases pass against the original on current `main`, including
not-a-number directions, and an independent review approved it. No frame-rate patch edits
either function.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0183ac60` | 20077 | `camera_update_follow` | edge-verified | Updates subject-relative camera position and orientation through input, chase, collision avoidance and territory constraints; applies selected frame-rate gains |
| `0x0183fad0` | 79 | `camera_follow_pitch` | edge-verified | Pitch from the forward basis; preserve scalar square underflow and zero fallback; call atan2 and XOR the returned sign bits |
| `0x0183fb20` | 43 | `camera_follow_yaw` | edge-verified | Yaw from the forward X/Z direction; exact signed-zero fallback preserves stored angle; unordered components reach atan2 unchanged |

## Structs and globals

None documented yet. Record each as: name, address, size, layout, who reads and writes it.

## Patch points

None documented yet. Record each as: address, what the original does there, what the
replacement or a mod would change, and the risk.

## Open questions

- Everything: this system has not been studied yet.
