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
| Target (Ghidra export) | 157,757 | 42,487,404 |
| Replaced | 245 (0.16%) | 114,180 (0.27%) |
| Edge-verified (includes verified) | 245 (0.16%) | 114,180 (0.27%) |
| Verified | 21 (0.01%) | 14,010 (0.03%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,264 (11.58%) | 176,606 (0.42%) |
| Edge-verified, with family members | 18,264 (11.58%) | 176,606 (0.42%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 1 | 1 | 1 | 1 | 2,693 |
| audio | 5 | 5 | 5 | 0 | 413 |
| camera | 3 | 3 | 3 | 0 | 20,199 |
| character | 1 | 1 | 1 | 1 | 979 |
| engine | 16 | 16 | 16 | 0 | 861 |
| event | 2 | 2 | 2 | 1 | 545 |
| frame_timing | 52 | 51 | 51 | 12 | 15,382 |
| input | 5 | 5 | 5 | 1 | 890 |
| kernel | 14 | 14 | 14 | 1 | 993 |
| loading | 7 | 7 | 7 | 0 | 990 |
| physics | 1 | 1 | 1 | 1 | 3,222 |
| render | 139 | 139 | 139 | 3 | 67,013 |
