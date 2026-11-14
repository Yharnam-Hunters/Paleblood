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
| Replaced | 775 (0.49%) | 264,486 (0.62%) |
| Edge-verified (includes verified) | 775 (0.49%) | 264,486 (0.62%) |
| Verified | 126 (0.08%) | 32,935 (0.08%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,794 (11.91%) | 326,912 (0.77%) |
| Edge-verified, with family members | 18,794 (11.91%) | 326,912 (0.77%) |
| Verified, with family members | 18,145 (11.50%) | 95,361 (0.22%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

Matching tier: 125 functions compile byte for byte to the original from their own C (clang-3.4.2, every reference checked; `symbols/matching.csv`). Matching is optional evidence and is not a separate status.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 45 | 45 | 45 | 7 | 10,680 |
| audio | 5 | 5 | 5 | 2 | 413 |
| camera | 5 | 5 | 5 | 0 | 20,790 |
| character | 10 | 10 | 10 | 1 | 3,203 |
| engine | 155 | 155 | 155 | 7 | 54,661 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 81 | 80 | 80 | 13 | 20,842 |
| input | 5 | 5 | 5 | 2 | 890 |
| kernel | 39 | 39 | 39 | 6 | 4,385 |
| loading | 87 | 87 | 87 | 6 | 24,489 |
| menu | 18 | 18 | 18 | 0 | 3,553 |
| param | 137 | 137 | 137 | 0 | 43,266 |
| physics | 10 | 10 | 10 | 1 | 4,711 |
| render | 177 | 177 | 177 | 79 | 72,058 |
