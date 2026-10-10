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

## Host v1 hook callback

`runtime/host_hooks.c` provides a Paleblood-owned implementation for the version-1
`install_hook(offset, size, target)` callback. The host calls `rt_host_hooks_initialize` with its
v1 interface before `bbgame_init`; the callback then writes a 14-byte x86-64 indirect absolute
jump at the image-relative function entry. This form preserves every guest argument register.
The host must keep the mapped image writable and executable until hook installation finishes.
The callback bounds-checks the complete function extent and rejects short functions and null
targets. It is an additive host component; the existing scaffold still owns full game loading and
execution.

Roadmap, in order: loader (done) → kernel, threads and memory → files → input → audio → video
out → GNM and shaders (the largest). `tools/boot.py` runs the boot harness and records how far
it gets (`symbols/boot.json`, `symbols/boot_history.csv`, `symbols/imports.csv`); the README's
progress table shows the import coverage and the furthest boot milestone. The boot history keeps
the outcome, phase, import return site, and guest fault location so changes to a failure's exact
execution point remain visible even when import totals and the blocking symbol stay the same.

Clean room: written from public documentation (NID databases, psdevwiki), our own
reverse engineering and observed behaviour. No code copied from, or modelled line by line on,
another emulator or runtime.
