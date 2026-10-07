# Architecture

## Placement rule

- `runtime/`, `test/` and the generic `tools/` hold nothing specific to the
  game: no title identity, no tracked function names or addresses. Everything
  game-specific is under `game/` and `symbols/`.
- `tools/check_agnostic.py` enforces it against `game/agnostic_words.txt` and
  every name and address in `symbols/functions.csv` and `game/hooks.csv`.
- If a feature needs game knowledge, the mechanism goes in `runtime/` and the
  knowledge goes in `game/` as data.

## Hook registry

`game/hooks.csv` maps an original address to its replacement:

```
address,replacement,system
0x00401000,bb_frame_timing_update_delta,frame_timing
```

- The row must match `symbols/functions.csv`: status `replaced` or `verified`,
  the same system, and `replacement` equal to `bb_` plus the name.
  `tools/validate_functions.py` checks both directions.
- `tools/gen_hooks.py` turns the CSV into `hooks_table.c` at build time, sorted by address,
  with each function's size from `symbols/functions.csv`.
- Replacements are defined in `game/<system>/*.cpp` (C++20, `extern "C"`) or `*.c`.
- Everything under `game/` plus `runtime/` builds into one shared library, `libbbgame.so`.

## Running in the game

The scaffold (`third_party/bbport`, our fork) loads the library when `BB_GAME_LIB` names it:
after mapping, relocating and patching the image, before any game code runs. It calls
`bbgame_init` (`runtime/plugin.c`), which records the image address for `rt_guest()` and asks
the loader to install every hook: a 14-byte absolute jump at the original function's entry.
Functions shorter than 14 bytes cannot be hooked this way; `validate_functions.py` refuses
them in `game/hooks.csv`. The loader refuses the library when the scaffold's executable check
is skipped, because the addresses are only valid for the target.

Replacement code reaches game memory and functions through `runtime/guest.h`:
`rt::ptr<T>(address)` and `rt::fn<Signature>(address)`, with PS4 addresses. Calling a library
function goes through the game's own thunk, so the scaffold's implementation answers it, as it
does for the original.

## Progress

`symbols/ghidra_functions.csv` is the denominator: address and size only, from
`tools/ghidra/ExportFunctions.java` (setup in [GHIDRA.md](GHIDRA.md)). `symbols/functions.csv` is what we track. The
README block is generated from them by `tools/progress.py`.
