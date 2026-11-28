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
- [Menu](menu.md)
- [Param](param.md)
- [Physics](physics.md)
- [Render](render.md)

## Progress

| | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Target (Ghidra export) | 157,757 | 42,487,404 |
| Replaced | 1,369 (0.87%) | 384,388 (0.90%) |
| Edge-verified (includes verified) | 1,369 (0.87%) | 384,388 (0.90%) |
| Verified | 285 (0.18%) | 70,459 (0.17%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 156 function families | 27,733 (17.58%) | 651,051 (1.53%) |
| Replaced, with family members | 29,102 (18.45%) | 1,035,439 (2.44%) |
| Edge-verified, with family members | 29,102 (18.45%) | 1,035,439 (2.44%) |
| Verified, with family members | 18,304 (11.60%) | 132,885 (0.31%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

Matching tier: 127 functions compile byte for byte to the original from their own C (clang-3.4.2, every reference checked; `symbols/matching.csv`). Matching is optional evidence and is not a separate status.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 59 | 59 | 59 | 31 | 19,421 |
| audio | 9 | 9 | 9 | 2 | 902 |
| camera | 8 | 8 | 8 | 0 | 21,151 |
| character | 12 | 12 | 12 | 6 | 3,823 |
| engine | 659 | 658 | 658 | 76 | 155,086 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 100 | 99 | 99 | 16 | 23,286 |
| input | 13 | 13 | 13 | 6 | 2,431 |
| kernel | 39 | 39 | 39 | 29 | 4,385 |
| loading | 96 | 96 | 96 | 8 | 24,939 |
| menu | 30 | 30 | 30 | 6 | 4,830 |
| param | 141 | 141 | 141 | 23 | 44,134 |
| physics | 10 | 10 | 10 | 1 | 4,711 |
| render | 193 | 193 | 193 | 79 | 74,744 |
