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

- The row must match `symbols/functions.csv`: status `replaced`, `edge-verified` or `verified`,
  the same system, and `replacement` equal to `bb_` plus the name.
  `tools/validate_functions.py` checks both directions.
- `tools/gen_hooks.py` turns the CSV into `hooks_table.c` at build time, sorted by address,
  with each function's size from `symbols/functions.csv`.
- Replacements are defined in `game/<system>/*.cpp` (C++20, `extern "C"`) or `*.c`.
- Everything under `game/` plus `runtime/` builds into one shared library, `libbbgame.so`.

## How replacements run

Today, replacements run in the verification harness (`tools/harness.py`, [VERIFY.md](VERIFY.md)):
our loader (`runtime/loader.c`) maps the executable, every system import is bound to a scripted
stub, and the original function and the replacement run side by side on the same inputs. The
harness calls the replacement directly; the hook registry above is what the runtime will install
once it runs the game (a 14-byte absolute jump at each original function's entry; shorter
functions cannot be hooked this way, and `validate_functions.py` refuses them).

Replacement code reaches the original's functions and globals through named declarations,
`RT_ORIGINAL` and `RT_GLOBAL` (`runtime/include/runtime/original.h`, STYLE.md), with PS4
addresses. Calling a library function goes through the game's own thunk, so whatever answers the
original (the harness's stubs, or the runtime) answers the replacement the same way.

## Our own runtime (`runtime/`)

Paleblood's own runtime, written clean-room from public documentation (the published PS4
formats, NID databases), our own reverse engineering and observed behaviour, with no code from
any emulator or other PS4 runtime. The borrowed runtime the project used until now (bbport) is
being removed. The game does not run on Paleblood until this roadmap is far enough along:

1. **Loader** (done): `runtime/loader.c` maps the executable and the game's own C library and file
   system modules, applies every relocation and binds each import through a callback. Its mapped
   image was checked byte-identical to the one the old runtime prepared, and the verification
   harness uses it.
2. **Generic capture:** recording a function's inputs from the runtime side, with no recording
   code inside replacements, so recordings of real play come from the runtime.
3. **Kernel, threads and memory**, then 4. **files**, 5. **input**, 6. **audio**, 7. **video out**.
8. **GNM and shaders**, the largest part: graphics are rebuilt at the engine level, by replacing
   the game's own graphics layer (GX) with our Vulkan code, rather than emulating the PS4's GPU.

How far the executable boots on the runtime is measured by the boot harness (`runtime/boot.c`,
`tools/boot.py`) and shown in the README; the work is split into claimable issues per system
call group (label `runtime`).

## Progress

`symbols/ghidra_functions.csv` is the denominator: address and size only, from
`tools/ghidra/ExportFunctions.java` (setup in [GHIDRA.md](GHIDRA.md)). `symbols/functions.csv` is what we track. The
README block is generated from them by `tools/progress.py`.
