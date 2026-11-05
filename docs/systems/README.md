<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Systems

One page per game system: summary, key functions, structs and globals, patch points and open questions. The function tables come from `symbols/functions.csv`.

- [Ai](ai.md)
- [Audio](audio.md)
- [Camera](camera.md)
- [Character](character.md)
- [Engine](engine.md)
- [Event](event.md)
- [Frame timing](frame_timing.md)
- [Input](input.md)
- [Kernel](kernel.md)
- [Loading](loading.md)
- [Physics](physics.md)
- [Render](render.md)

## Progress

| | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Target (Ghidra export) | 157,757 | 42,487,347 |
| Replaced | 121 (0.08%) | 32,208 (0.08%) |
| Edge-verified (includes verified) | 121 (0.08%) | 32,208 (0.08%) |
| Verified | 21 (0.01%) | 14,010 (0.03%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 1 | 1 | 1 | 1 | 2,693 |
| audio | 5 | 5 | 5 | 0 | 413 |
| camera | 2 | 2 | 2 | 0 | 122 |
| character | 1 | 1 | 1 | 1 | 979 |
| engine | 15 | 15 | 15 | 0 | 843 |
| event | 2 | 2 | 2 | 1 | 545 |
| frame_timing | 43 | 42 | 42 | 12 | 14,394 |
| input | 2 | 1 | 1 | 1 | 426 |
| kernel | 14 | 14 | 14 | 1 | 993 |
| loading | 0 | 0 | 0 | 0 | 0 |
| physics | 1 | 1 | 1 | 1 | 3,222 |
| render | 37 | 37 | 37 | 3 | 7,578 |
