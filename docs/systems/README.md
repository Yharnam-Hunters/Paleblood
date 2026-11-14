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
| Replaced | 842 (0.53%) | 277,997 (0.65%) |
| Edge-verified (includes verified) | 842 (0.53%) | 277,997 (0.65%) |
| Verified | 165 (0.10%) | 42,736 (0.10%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,861 (11.96%) | 340,423 (0.80%) |
| Edge-verified, with family members | 18,861 (11.96%) | 340,423 (0.80%) |
| Verified, with family members | 18,184 (11.53%) | 105,162 (0.25%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

Matching tier: 127 functions compile byte for byte to the original from their own C (clang-3.4.2, every reference checked; `symbols/matching.csv`). Matching is optional evidence and is not a separate status.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 51 | 51 | 51 | 28 | 15,264 |
| audio | 5 | 5 | 5 | 2 | 413 |
| camera | 8 | 8 | 8 | 0 | 21,151 |
| character | 11 | 11 | 11 | 5 | 3,511 |
| engine | 162 | 161 | 161 | 10 | 55,573 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 98 | 97 | 97 | 14 | 23,140 |
| input | 9 | 9 | 9 | 6 | 1,738 |
| kernel | 39 | 39 | 39 | 6 | 4,385 |
| loading | 93 | 93 | 93 | 7 | 24,807 |
| menu | 30 | 30 | 30 | 5 | 4,830 |
| param | 141 | 141 | 141 | 0 | 44,134 |
| physics | 10 | 10 | 10 | 1 | 4,711 |
| render | 185 | 185 | 185 | 79 | 73,795 |
