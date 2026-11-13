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
- [Param](param.md)
- [Physics](physics.md)
- [Render](render.md)

## Progress

| | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Target (Ghidra export) | 157,757 | 42,487,404 |
| Replaced | 555 (0.35%) | 218,157 (0.51%) |
| Edge-verified (includes verified) | 555 (0.35%) | 218,157 (0.51%) |
| Verified | 114 (0.07%) | 29,116 (0.07%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,574 (11.77%) | 280,583 (0.66%) |
| Edge-verified, with family members | 18,574 (11.77%) | 280,583 (0.66%) |
| Verified, with family members | 18,133 (11.49%) | 91,542 (0.22%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

Matching tier: 118 functions compile byte for byte to the original from their own C (clang-3.4.2, every reference checked; `symbols/matching.csv`). Matching is optional evidence and is not a separate status.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 1 | 1 | 1 | 1 | 2,693 |
| audio | 5 | 5 | 5 | 2 | 413 |
| camera | 3 | 3 | 3 | 0 | 20,199 |
| character | 1 | 1 | 1 | 1 | 979 |
| engine | 114 | 114 | 114 | 5 | 41,884 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 63 | 62 | 62 | 13 | 16,499 |
| input | 5 | 5 | 5 | 2 | 890 |
| kernel | 22 | 22 | 22 | 6 | 1,994 |
| loading | 42 | 42 | 42 | 2 | 14,401 |
| param | 137 | 137 | 137 | 0 | 43,266 |
| physics | 1 | 1 | 1 | 1 | 3,222 |
| render | 154 | 154 | 154 | 79 | 69,444 |
