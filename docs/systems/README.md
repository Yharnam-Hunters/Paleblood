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
| Replaced | 747 (0.47%) | 257,323 (0.61%) |
| Edge-verified (includes verified) | 747 (0.47%) | 257,323 (0.61%) |
| Verified | 124 (0.08%) | 31,819 (0.07%) |

Counts are cumulative: verified functions are included in edge-verified and replaced totals.

| With function families | Functions (of target) | Bytes (of target) |
|---|---:|---:|
| Members of 19 function families | 18,019 (11.42%) | 62,426 (0.15%) |
| Replaced, with family members | 18,766 (11.90%) | 319,749 (0.75%) |
| Edge-verified, with family members | 18,766 (11.90%) | 319,749 (0.75%) |
| Verified, with family members | 18,143 (11.50%) | 94,245 (0.22%) |

Family members are tiny functions (one to a few bytes each) that share one replacement per family: they add far more to the function count than to the bytes.

Matching tier: 125 functions compile byte for byte to the original from their own C (clang-3.4.2, every reference checked; `symbols/matching.csv`). Matching is optional evidence and is not a separate status.

| System | Tracked | Replaced | Edge-verified (includes verified) | Verified | Replaced bytes |
|---|---:|---:|---:|---:|---:|
| ai | 42 | 42 | 42 | 6 | 8,189 |
| audio | 5 | 5 | 5 | 2 | 413 |
| camera | 5 | 5 | 5 | 0 | 20,790 |
| character | 5 | 5 | 5 | 1 | 2,331 |
| engine | 149 | 149 | 149 | 6 | 53,595 |
| event | 2 | 2 | 2 | 2 | 545 |
| frame_timing | 80 | 79 | 79 | 13 | 20,578 |
| input | 5 | 5 | 5 | 2 | 890 |
| kernel | 39 | 39 | 39 | 6 | 4,385 |
| loading | 83 | 83 | 83 | 6 | 22,942 |
| menu | 16 | 16 | 16 | 0 | 3,305 |
| param | 137 | 137 | 137 | 0 | 43,266 |
| physics | 4 | 4 | 4 | 1 | 4,134 |
| render | 176 | 176 | 176 | 79 | 71,960 |
