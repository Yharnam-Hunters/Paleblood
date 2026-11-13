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
| Replaced | 370 (0.23%) | 168,326 (0.40%) |
| Edge-verified (includes verified) | 370 (0.23%) | 168,326 (0.40%) |
| Verified | 100 (0.06%) | 25,301 (0.06%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,389 (11.66%) | 230,752 (0.54%) |
| Edge-verified, with family members | 18,389 (11.66%) | 230,752 (0.54%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 1 | 1 | 1 | 1 | 2,693 |
| audio | 5 | 5 | 5 | 2 | 413 |
| camera | 3 | 3 | 3 | 0 | 20,199 |
| character | 1 | 1 | 1 | 1 | 979 |
| engine | 52 | 52 | 52 | 5 | 25,089 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 52 | 51 | 51 | 13 | 15,382 |
| input | 5 | 5 | 5 | 2 | 890 |
| kernel | 14 | 14 | 14 | 2 | 993 |
| loading | 31 | 31 | 31 | 2 | 11,560 |
| physics | 1 | 1 | 1 | 1 | 3,222 |
| render | 139 | 139 | 139 | 69 | 67,013 |
