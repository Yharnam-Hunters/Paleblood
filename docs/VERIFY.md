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

Runtime-generated cases may also include optional provenance, without changing the schema:

```json
"capture": {"function": "frame_timing_get_monotonic_ms", "run_id": "20261006-run2"}
```

The function name and run ID bind the case to the hooked function and one capture process. During
`verify.py run`, provenance is checked against the requested function and the case's `address` is
checked against that function's address. Legacy schema-1 cases without `capture` remain supported.
Run IDs are filename-safe; case files are created exclusively with owner-only permissions, so a
reused run ID and case number cannot silently replace an existing recording.

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
- XMM input strings are raw little-endian hexadecimal bytes, without a `0x` prefix. Short
  values are padded with zero bytes to 16 bytes; an empty string supplies zero in every lane.
  The differential frontend rejects malformed strings and values longer than 16 bytes,
  rather than treating them as GP address expressions or silently dropping upper bytes.
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

## Optional instruction-level memory trace

On Linux x86-64 with Capstone 5 installed, a case can also single-step instructions in the
declared guest executable and replacement shared-object ranges. The CI pins Capstone 5.0.9. The
tracer prefers the matching shared library bundled with the optional Python wheel, then falls back
to the installed system library:

```sh
python3 tools/harness.py --elf "$BB_ELF" --case "$CASE" --address 0xADDRESS \
    --trace-instructions --trace-instruction-limit 100000
```

`tools/verify.py run` and the catalog-backed `tools/differential.py differential` command accept
the same `--trace-instructions` and `--trace-instruction-limit` options. Traces record the
instruction identity/address, decoded memory operand, read/write class, byte width, effective
address, declared region and offset, pre-access bytes, post-write bytes, and a bounded GPR snapshot.
The bounds check uses the exact declared buffer length rather than its mapped redzone size, so
unaligned, overlapping, aliased, and same-value writes are reported. Accesses to adjacent but
undeclared bytes remain out of bounds even when the process mapping permits them.

The trace is deliberately fail-closed. An undeclared/out-of-bounds access, missing byte snapshot,
unsupported memory operand, instruction/event limit, or skipped host-call body marks the trace
incomplete; differential comparison reports it as a failure. A host call is listed as a gap
because its native instructions are outside the ranges being traced. When a guest import thunk
enters a harness-generated import bridge, the trace identifies the scripted import by name and
records an `external_import_dispatch` gap. The bridge and its C/Python dispatcher are not
single-stepped, so the trace and CFG remain incomplete and cannot qualify for automated
verification. Existing scripted import and stub records remain a separate mediated-access channel
and are not presented as instruction-level events.

For example, the recorded `frame_timing_get_monotonic_ms` canary reaches guest address
`0x802bbfe58`, where an indirect jump enters the harness bridge for `sceKernelClockGettime`.
That boundary explains why the trace is incomplete; it does not establish execution inside the
host dispatcher or prove behavior beyond the recorded guest instructions.

### Import-boundary observations

Instruction tracing also emits `import_observations` for recognized harness bridges that have
a return address in an instrumented range. These are **entry/return snapshots**, not instruction
traces of the import. Each record identifies the original/replacement side, import symbol, stable
return location, raw GPR values before/after, and selected memory bytes before/after. Registers
are raw observations; the observer does not infer argument types or ABI-defined results.

The observer samples the active stack from boundary RSP toward the target's entry return slot
(at most 512 bytes), then declared fixture buffers in deterministic order, within a shared
4,096-byte budget per import and 32 observations per fixture. It reports omitted bytes, truncated
regions, unreadable slices as `null`, and returns not observed. A changed offset reports a net
byte difference, not the instruction or number of writes that produced it. Stack sampling does
not declare those bytes valid import arguments. Snapshots remain private fixture evidence.

Host reads, same-value writes, temporary changes restored before return, memory outside those
slices, floating-point/vector state, external effects, and the host CFG remain unobserved. Raw
registers and snapshot bytes are not compared as if all were ABI-defined observable state. The
existing mediated-access log remains separate. Comparison, automation acceptance, and CFG
feedback reject external-execution evidence even if a result incorrectly labels its trace or CFG
complete. Import snapshots cannot qualify a function for verification.

The smallest safe real-function trial uses a sufficiently evidenced, recorded, import-free path
whose instructions and memory accesses stay within the supported trace scope. A real import body
would instead need supported native instruction execution, declared memory/TLS state, and full
traceable control flow, including any further calls or system transitions. Expanding the code
range to arbitrary C/Python host libraries or replaying recorded writes does not meet that
requirement. Until that prerequisite exists, the clock regression must continue to fail closed.

The first-divergence report compares memory events in execution order and reports the earliest
different recorded event with both sides' instruction addresses and register snapshots. Original
and replacement PCs are not aligned: different instruction counts and addresses are allowed. An
event-trace difference with equal final observable results is labeled a bounded internal-path
difference, not a behavioral failure. If final return values, writes, calls, or observed registers
differ but no earlier memory-event difference exists, the report says that no earlier memory
divergence was observed. This is not instruction-by-instruction register comparison and does not
claim an exact machine-state first divergence.

When instruction tracing is enabled, each side also reports `control_flow_coverage`. It records
executed basic-block entries and control-flow edges, including conditional outcomes, calls,
returns, indirect transfers, and loop backedges. Original addresses use guest virtual addresses;
replacement addresses use module-relative offsets so ASLR does not change the report. Differential
JSON maps each block and edge back to the fixture IDs that exercised it and retains a per-fixture
coverage record. Human-readable evidence bundles summarize the same results.

Coverage has explicit `complete`, `partial`, and `unavailable` states. `unavailable` means no
usable instruction path was observed. It is distinct from a complete trace with zero conditional
branch edges; return edges can still be present in that result.
Partial traces, step-limit truncation, unknown control transfers,
or failed instruction tracing are never used as fixture-prioritization evidence. A trace with a
memory or execution-scope failure may retain observed CFG data, but its `feedback_eligible` value
is false.

After collecting differential JSON, pass it to bounded ABI-guided generation to prioritize seeds
that introduced new blocks/edges or reached a conditional branch whose other outcome is not yet
observed:

```sh
python3 tools/differential.py explore 0x0111a7f0 --cases CASES --out GENERATED \
    --max-fixtures 64 --seed 7 --coverage-from previous-differential.json
```

The report hash and per-seed reasons are recorded on generated fixtures. Only complete prior
coverage contributes a score; fixtures without a matching complete result remain explicitly
`unknown` and keep deterministic input order after evidence-ranked seeds. This prioritizes seeds
for existing typed boundary, float-edge, nullability, and explicitly ABI-approved alias variants;
it does not infer branch predicates, invent pointer contracts, change fixture values beyond the
existing ABI-guided generator, or establish new game behavior. Coverage is per observed fixture,
not a proof that every block or edge in the executable has been found.

This increment observes supported Capstone-classified explicit x86-64 memory operands and the
modeled implicit stack effects described in the [instruction-trace support matrix](INSTRUCTION_TRACE_SUPPORT.md),
and records bounded dynamic CFG coverage for supported instruction traces.
Known string/repeat, VEX/EVEX memory, system, atomic, and special-memory instruction families are
rejected before execution. The tracer does not observe arbitrary host-library instructions,
asynchronous accesses, or complete static CFG discovery. Dynamic coverage is limited to the
executed path in each fixture. A complete trace means complete only for the matrix's
supported scope; Capstone does not identify every possible implicit-memory instruction, so a newly
discovered family must be added to the rejection classification before relying on it. Single-
stepping is slow and intended for bounded verification fixtures, not boot runs. The tracer
requires Linux `ptrace` permission and the Capstone 5 detail ABI. It records no executable bytes
or game captures.

Per Intel's [POP instruction semantics](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf),
for `POP` to a memory destination based on `RSP`/`ESP`, the implicit stack read is traced at the
pre-instruction stack pointer, then the destination effective address is calculated with the
stack pointer advanced by the popped operand width. This covers direct, displacement, and SIB
indexed forms such as `[rsp]`, `[rsp+disp]`, and `[rsp+index*scale+disp]`. The destination is
bounds-checked at that post-increment address. Unknown POP destination access classifications or
unsupported operand/address sizes make the trace incomplete. `PUSH [rsp]` remains different: its
explicit source address uses pre-decrement RSP, and its implicit write is at RSP minus the pushed
width.

The deterministic synthetic regression tests run with the repository test suite:

```sh
BB_LOADER=build/runtime/libpbloader.so python3 -m unittest test.test_harness test.test_differential
```

These tests validate instrumentation behavior only. They are not original-game fixtures and do not
increase any function's verification status.

## From edge-verified to verified

A function whose edge cases pass is `edge-verified` (CONTRIBUTING.md, "Status"). Once cases
recorded in the game exist for it (`CAPTURES/NAME/*.json`), `tools/promote.py` runs `verify.py`
on them and on the edge cases again, and sets `verified` only if both pass completely. It runs
at session end (`tools/end_session.sh`); `--dry-run` shows what it would do. Comparison result
JSON is written to a temporary directory outside the capture library and discarded after each
run, so promotion does not add generated files beside the source cases.

## Recorded results and quarantine

`verify.py run --record CASES` writes the run's counts to `symbols/verification.csv`, one row
per function, case set (`recorded`, `edge`, or `option-recorded`: recorded in the game with an
option on) and option (`--option LABEL`, needed with `--patch` or `--env`). The row carries a
hash of the replacement's source, its defining file and the headers it includes, so a result
belongs to the code it was run on; after any change, run the sets again. The file holds counts
only, never case data.

A case that can't be used goes in `symbols/quarantine.csv` (function, case set, option, case
file name without `.json`, reason, date), and only when it can't be fixed: for example a
recording made under an earlier version of an option, which lacks inputs the current option
needs. A quarantined case that fails counts as quarantined, not failed. Quarantine is not for a
mismatch between the original and the replacement: that is a bug to fix.

`tools/validate_functions.py` (CI) refuses `verified` for a function without current `recorded`
and `edge` results without an option, with any failure that is not quarantined in any of its
current results, or with a result for other code. `promote.py` records the runs it makes and
applies the same rule.

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

## Differential engine (Phase 2)

`tools/differential.py` orchestrates the same Paleblood loader and guest-instruction harness, then
compares original-instruction results with a registered Paleblood replacement. It uses the
existing Phase 1 SQLite catalog (`--state` selects that database) for resumable case results and
run state; it does not keep a second catalog or change function verification status.

Before execution, provide `abi.json` in the fixture directory or an `abi` object in every fixture.
The manifest states the supported `sysv_amd64` calling convention, evidence, 16-byte stack
alignment, typed arguments and return, declared input and writable buffers, and a bounded timeout.
Missing or uncertain ABI evidence blocks execution. Fixture memory values and pointer relationships
must be fixed and serializable. The runner uses the fixture timeout by default; `--timeout` can
only reduce that limit. The guest entry fixture is populated according to the declared PS4 x86-64
System V register/stack convention. A replacement is called as a native Linux x86-64 function;
only the explicitly shared convention is compared, and the engine does not emulate PS4 process or
kernel behavior.

Example using the existing edge fixture generator for `event_flag_write`:

```sh
STATE="${XDG_CACHE_HOME:-$HOME/.cache}/paleblood/function-catalog.sqlite3"
python3 game/event/flag_write_cases.py "$BB_CAPTURES/event_flag_write/edge"
tools/differential.py --state "$STATE" fixtures 0x017cfcc0 \
    --cases "$BB_CAPTURES/event_flag_write/edge"
tools/differential.py --state "$STATE" differential 0x017cfcc0 \
    --cases "$BB_CAPTURES/event_flag_write/edge" \
    --function event_flag_write --elf "$BB_ELF" --lib build/game/libbbgame.so --resume
tools/differential.py --state "$STATE" evidence 0x017cfcc0 --out "$HOME/bb-backup/differential-evidence"
```

The differential command requires a current catalog qualification or an existing registered
replacement. Its cache key includes executable, fixture, ABI, catalog/qualification, replacement
source and binary, loader, harness, verifier, and engine identities. `--resume` reuses a side only
when the complete key and fixture identity match. Evidence bundles contain hashes and fixture
definitions but no executable bytes; keep them outside public repositories because real capture
fixtures may contain private game state. The minimizer is bounded and writes both the untouched
failing fixture and its minimized form.

`explore` performs seeded, bounded variation for explicitly typed integers and float/double ABI
arguments. It includes zero, signed/unsigned boundaries, finite floating-point edges, infinities,
NaNs, and alias cases only for pointer arguments whose ABI record explicitly sets
`alias_safe: true`. It is not coverage guided. Each result is still a candidate fixture and must
be reviewed for contract validity before execution.

The harness records its mediated scripted import/stub reads and writes in `access_trace`, including
direction, byte count, region, and source. It also checks a deterministic byte redzone after each
declared buffer, reporting the first changed offset; the existing guard page catches accesses past
the allocation page. The redzone detects guest writes into padding, but cannot observe reads,
same-value writes, or the exact instruction that accessed a byte. Direct guest memory instructions
are not dynamically traced. The harness still compares return values, ABI-preserved state,
explicitly selected caller-saved registers, scripted calls, and final changed writable bytes.

Instruction-level first-divergence state traces and control-flow/CFG coverage remain unavailable.
No coverage feedback is used to steer exploration. These are blockers for broad automated batch
decompilation. A match is evidence for supplied fixtures only; it does not promote a function or
establish observed original-game behavior.
