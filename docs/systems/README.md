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
| Replaced | 739 (0.47%) | 255,471 (0.60%) |
| Edge-verified (includes verified) | 739 (0.47%) | 255,471 (0.60%) |
| Verified | 119 (0.08%) | 31,226 (0.07%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,758 (11.89%) | 317,897 (0.75%) |
| Edge-verified, with family members | 18,758 (11.89%) | 317,897 (0.75%) |
| Verified, with family members | 18,138 (11.50%) | 93,652 (0.22%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

Matching tier: 125 functions compile byte for byte to the original from their own C (clang-3.4.2, every reference checked; `symbols/matching.csv`). Matching is optional evidence and is not a separate status.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 42 | 42 | 42 | 1 | 8,189 |
| audio | 5 | 5 | 5 | 2 | 413 |
| camera | 5 | 5 | 5 | 0 | 20,790 |
| character | 5 | 5 | 5 | 1 | 2,331 |
| engine | 147 | 147 | 147 | 6 | 53,527 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 77 | 76 | 76 | 13 | 19,528 |
| input | 5 | 5 | 5 | 2 | 890 |
| kernel | 39 | 39 | 39 | 6 | 4,385 |
| loading | 80 | 80 | 80 | 6 | 22,208 |
| menu | 16 | 16 | 16 | 0 | 3,305 |
| param | 137 | 137 | 137 | 0 | 43,266 |
| physics | 4 | 4 | 4 | 1 | 4,134 |
| render | 176 | 176 | 176 | 79 | 71,960 |
