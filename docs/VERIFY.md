# verify.py

`tools/verify.py run` shows that a replacement behaves like the original on the same inputs.
It runs the original guest function and the native replacement, natively, on the CPU, without
the GPU and without starting the game, then compares what they did.

```
verify.py run --function frame_timing_get_monotonic_ms --captures DIR \
    --lib build/game/libbbgame.so
```

- `--elf`: your own dump's executable, `elf/eboot.elf` (written by `tools/prepare_dump.sh`).
  Default `$BB_ELF`, then `$BB_DATA_ROOT/elf/eboot.elf`. The harness maps it with Paleblood's own
  loader (`runtime/loader.c`, built as `build/runtime/libpbloader.so`): segments copied in,
  relocations applied, every import bound to a scripted stub.
- `--lib`: the game library built from this repository. Default `$BB_GAME_LIB`, then
  `build/game/libbbgame.so`.
- `--captures`: a directory of case files (below). Results go to `DIR/results/` (or `--out`).

Exit codes: 0 every case matches, 1 a mismatch, 3 bad input. The JSON it prints goes into the
pull request.

## How a case runs

`tools/harness.py` maps the image once per side and runs each case in a fork of it, so a crash
or a hang loses only that case. It runs at low priority (`BB_NICE`, default 10). For each case:

1. Maps the executable with our loader (`runtime/loader.c`) at the runtime's host address: segments, relocations, imports bound to stubs.
2. Turns every imported library function into a stub. The case scripts each call: its return
   value and the bytes it writes through its pointer arguments. A call the case does not script
   is recorded as an error.
3. Lays out the case's buffers in a sandbox, writes its memory presets (values, or pointers to
   buffers) into the image.
4. Calls the original at its address, or the replacement from the game library (after its
   `bbgame_init`, given a loader that installs no hooks), through a trampoline that loads the
   argument registers, puts sentinels in the callee-saved registers and records the return
   registers.
5. Compares every writable byte of the image and of the buffers before and after.

Both sides see the same memory and the same library behavior, so any difference is the
replacement's.

## Where inputs come from

Case files hold inputs from your own dump and never go into the repository, an issue or a pull
request. Keep them under `$BB_CAPTURES` (for example `/mnt/.../captures/<function>/`).

- **Real runs.** Inputs recorded while the game ran: one directory per function,
  `$BB_CAPTURES/<function>/*.json`. Verification reuses everything recorded so far: point
  `--captures` at the function's directory. Replacements do not record their own inputs
  (STYLE.md: no recording code under `game/`); recording is the runtime's job, so new recordings
  come once Paleblood's runtime runs the game. Until then a new function is `edge-verified`, and
  `tools/promote.py` upgrades it when recordings for it exist.
- **Edge cases.** A generator next to the replacement writes the situations a short run does
  not reach (`game/<system>/*_cases.py`).

## Case file (schema 1)

```json
{
  "schema": 1,
  "address": "0x0111a7f0",
  "id": "first_call",
  "returns": "i32",
  "args": {"rdi": "buf:out"},
  "buffers": {"out": {"size": 4}, "state": {"size": 20, "bytes": "00...e8030000"}},
  "memory": [{"addr": "0x056d6ae8", "pointer": "state"}],
  "imports": [{"name": "sceKernelClockGettime", "argc": 2, "ret": 0,
               "writes": [{"arg": 1, "offset": 0, "bytes": "e803000000000000..."}]}]
}
```

- `args`: `rdi`..`r9`. A number, `buf:NAME[+off]`, or `guest:0xADDRESS` (a PS4 address).
- `buffers`: sandbox memory, laid out in name order; `bytes` is hex, zero-padded to `size`.
- `memory`: written before the call, at an image address or at `buf:NAME+off`: `bytes`,
  `pointer` (a buffer's address) or `guest` (the host address of a PS4 address, for example a
  game function a fake vtable points at, which the case then stubs).
- `imports`: scripted calls in order, per library function. `name` is the function name (its
  NID is computed); `argc` arguments are recorded and compared. A write may give `size`: a
  write whose bytes do not have that length is reported as an error (a truncated capture).
  For long runs of the same call (a wait that polls the clock), one entry can stand for many
  calls: `"series": {"arg": 0, "format": "timeval_us", "values": [t0, t1, ...]}` writes the
  next value (microseconds, as a `struct timeval`) on each call, one call per value. A native
  stub serves a series without entering Python, and the run records it once with a `count`;
  both sides are recorded the same way, so the number of calls is still compared.
- `stubs`: game functions to replace by scripted stubs, `[{"address": "0x...", "ret": 1,
  "argc": 0}]`, consumed in order per address. Use it for calls whose result the case must fix
  (a flag the real run computed elsewhere). Stubbed calls are recorded and compared like
  library calls, as `{"call": address}`. `ret` may be `buf:NAME` (an allocator returning a
  buffer); `argi` picks which argument registers are compared (a register the caller leaves
  undefined must not be); `argmem: [{"arg": 1, "size": 16}]` also compares the bytes an
  argument points at (`"offset"` starts further in); `argf32: [0]` compares the float in xmm0
  (`1`: xmm1) as its bit pattern (for stubs and imports); a function that returns a float is
  scripted with `ret` as the float's bit pattern (`"ret": "0x3f800000"`, for stubs and
  imports); case `args` may set `xmm0` and `xmm1` (hex bytes) for float arguments; `writes` stores what the real function stored through an argument
  (`bytes`, `pointer` to a buffer, or `guest` address), like library writes.
- `stack`: stack arguments, in order (the 7th integer argument first), as numbers, `buf:` or
  `guest:` values; the harness pushes them before the call and pops them after.
- `gs`: the guest thread pointer (`buf:NAME+off`), for code that uses thread-local storage. As
  the runtime's loader does, the harness rewrites the game's `mov rax, fs:[0]` to read GS; the
  case puts the pointer itself at `gs:[0]` (a `memory` entry), as a TCB holds it, and the
  thread-local variables below it.
- `returns`: `i8`, `i32`, `i64`, `i128`, `f32`, `f64`, `vec` or `void`: which return registers count.

## Result and comparison

Per case: `ret`, `preserved` (callee-saved registers `kept` or their value, and whether `rsp`
balanced), `writes` (`addr` is a PS4 address, or `buf:NAME+off`), `calls`, `errors`, or `fault`
when the process crashed. Two results match when the return registers, the preserved
registers, the final value of every written byte, and the library calls with their recorded
arguments are equal. Pointer arguments into the stack are recorded as `stack`, since frames
differ. Write order is not compared.

## Limits

- The function runs alone: whatever it calls inside the game runs too, as original code,
  unless the case stubs it.

## From edge-verified to verified

A function whose edge cases pass is `edge-verified` (CONTRIBUTING.md, "Status"). Once cases
recorded in the game exist for it (`CAPTURES/NAME/*.json`), `tools/promote.py` runs `verify.py`
on them and on the edge cases again, and sets `verified` only if both pass completely. It runs
at session end (`tools/end_session.sh`); `--dry-run` shows what it would do.

## Community patches and hooks

A hooked function's original bytes no longer run, so a byte patch inside it does nothing while
the hook is installed. `tools/patch_overlap.py PATCHES.xml` lists such patch lines (exit 1 when
there are any); `--map` lists every patch line with the function it falls in. Run it before
hooking a function that a community patch edits, and give the replacement an option for what
the patch did (as `BB_TARGET_FPS` does for the frame-rate patches).

### Checking an option against the patch it replaces

An option that does what a community patch did (`BB_TARGET_FPS`) is verified against the
original with that patch applied:

    tools/verify.py run --function frame_timing_frame_step --captures DIR \
        --patch "$BB_PATCHES:60 FPS++" --env BB_TARGET_FPS=60

`--patch FILE:NAME` writes the patch's bytes into the image of both sides
(`tools/harness.py --patch/--patch-name`): in the game the option runs together with the patch,
so the patch's other edits (shared constants in the data segment, other functions) are in
place for the replacement too, while its edits inside the replaced function never run. `--env`
sets the replacement side's environment. Cases
must give both sides enough script for the patched behaviour (a 1/30 s wait needs a longer clock
than a 1/60 s one; both sides running out of script is inconclusive, not a match).

`tools/reloc_refs.py ELF ADDRESS...` lists the relocated pointers (vtable and table slots) to a
function: with no direct caller and no relocated pointer, a function never runs in the stock game,
whatever a patch does to it.

## Which functions run: call counting probes

`$BB_PATCHES` above is your own copy of the community patch file (the project does not ship it).

These probes ran on the borrowed runtime, which is being removed; they come back with the runtime's
generic capture. Until then, `tools/reloc_refs.py` (above) and the call graph tell whether a function
can run at all. On the old runtime, before replacing a candidate, a game run showed whether it is
reached:

    tools/draft.sh ADDRESS...                      # instruction boundaries
    tools/probe_list.py --elf ELF ADDRESS... > probes.txt
    BB_PROBES=probes.txt BB_PROBE_OUT=counts.txt BB_CAPTURE=0 tools/run_game.sh 170 hook

`runtime/probe.c` puts a 5-byte jump at each entry to a stub that counts the call, runs the
displaced instructions and jumps back; the function itself is unchanged. Counts are rewritten
every 2 s. Entries whose bytes differ (hooked or patched) are skipped, and prologues that cannot
move (RIP-relative, branches) are refused by `probe_list.py`.
