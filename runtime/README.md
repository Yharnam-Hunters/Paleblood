# runtime

Paleblood's own PS4 runtime, game-agnostic: nothing here may name the game, a tracked function,
or an address (`tools/check_agnostic.py` enforces it). If a feature needs game knowledge, the
mechanism lives here and the knowledge lives in `game/` as data.

| Part | Files | State |
|---|---|---|
| Loader | `loader.c`, `include/runtime/loader.h` | maps a PS4 executable: segments, relocations, imports bound through a callback, TLS template, thread-pointer loads moved to GS |
| Boot harness | `boot.c` (`pbboot`) | runs the executable's entry on the runtime; stops at the first unimplemented import and names it |
| System libraries | `syslib/*.c`, `include/runtime/syslib.h` | implementations by library and symbol (`RT_SYSLIB`), one file per system call group |
| NIDs | `nid.c`, `include/runtime/nid.h` | symbol name to NID |
| Hooks and captures | `hooks.c`, `capture.c`, `include/runtime/recorder.hpp`, `probe.c`, `plugin.c` | hook registry and opt-in case writer; production hook dispatch does not yet observe calls or connect the Recorder to replacements; serialized cases carry function/run provenance and stable buffer ordering |

Roadmap, in order: loader (done) → kernel, threads and memory → files → input → audio → video
out → GNM and shaders (the largest). `tools/boot.py` runs the boot harness and records how far
it gets (`symbols/boot.json`, `symbols/boot_history.csv`, `symbols/imports.csv`); the README's
progress table shows the import coverage and the furthest boot milestone.

Clean room: written from public documentation (NID databases, psdevwiki), our own
reverse engineering and observed behaviour. No code copied from, or modelled line by line on,
another emulator or runtime.
