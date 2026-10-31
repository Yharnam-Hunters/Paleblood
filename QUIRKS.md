# Quirks

Traps in Ghidra and the PS4 ABI that cost time. Entries marked *unconfirmed*
come from general PS4/Orbis knowledge and have not been checked on this target
yet; confirm or correct them when you hit them.

## The converted executable

- The ELF has no section headers (confirmed). The Orbis loader (GhidraOrbis) synthesizes
  blocks from the program headers: the executable `PT_LOAD` becomes `.text`, and its own
  heuristic splits `.rodata`, `.eh_frame` and `.eh_frame_hdr` off the end of it. The progress
  denominator is the functions Ghidra finds in that `.text` block, so it depends on the loader
  and the analysis. `progress.py` ties its numbers to the hash of the committed export, and the
  export prints how many functions fell outside `.text`. `.eh_frame` and `.eh_frame_hdr` are
  marked executable by the loader; they are data.
- `e_type` is `0xfe10` (confirmed), not `ET_EXEC` or `ET_DYN`. The first `PT_LOAD` has
  `p_vaddr` 0, so the file does not say where the game lives; the convention below does.
  Ghidra's default for this file type is *unconfirmed*: always set the image base
  explicitly on import and check it (the export script refuses any other base).

- Program headers include Sony types (`PT_SCE_*`, `PT_SCE_DYNLIBDATA` and
  friends) that Ghidra does not understand. The `PT_INTERP` is
  `/libexec/ld-elf.so.1` (confirmed). Dynamic linking data is in a Sony-specific
  segment, so Ghidra will not resolve imports by itself.
- SelfUtil-Patched removes the repeated data before the first segment (at
  0x4000) and patches the version segment. Another SelfUtil build can produce a
  different file with different hashes: always check `target.sha256`, and check
  the loaded image too (`target.image.sha256`), which no build choice changes.
- The v1.09 executable is not region-specific (confirmed 2026-10-11).
  Evidence: a US package - `CUSA00900`, content id
  `UP9000-CUSA00900_00-BLOODBORNE000000`, app version 1.09 merged over the
  base, region and title id read from `sce_sys/param.sfo` - converted with the
  pinned SelfUtil build (`tools/selfutil/build.sh`, commit `53ca642b`) into
  ELFs that reproduce every hash in `target.sha256`: `eboot.elf`
  `ca8813862d7b1345…` and all seven system modules. It also reproduces the
  build-independent loaded image in `target.image.sha256`,
  `071df19c8880086d…`. `tools/check_target.py <elf-dir>` prints the nine
  `ok` lines. So the hashes decide whether a dump lines up with our
  addresses; the title id does not. Two corollaries: a pre-patched executable
  (a frame-rate or debug-menu repack) breaks the loaded-image pin, so that pin
  doubles as an integrity check; and an untested region or version (JP
  `CUSA00208`, any other AppVer) stays unknown until its own check says `ok`.

## Address conventions

All addresses in this repository are **PS4 virtual addresses: the ELF `p_vaddr` plus
`0x400000`**, with Ghidra's image base set to `0x400000`. Community patch files use the same
address convention.

Three conventions exist, one constant apart. V is the ELF `p_vaddr`.

| Convention | Address | Where it appears |
|---|---|---|
| ELF virtual address | V | ELF program headers and file layout |
| PS4 virtual address | V + 0x400000 | **ours**, Ghidra, community patch files |

To convert an address to the ELF virtual address, subtract `0x400000`.
`tools/validate_functions.py` rejects any address below `0x00400000`, which catches a base-0
import by mistake.

Checked against the target image (read-only):
- The community "Performance Patch" address `0x0261B108` minus `0x400000` holds
  `c7 45 b4 06 00 00 00` (`mov dword [rbp-0x4c], 6`), which the patch changes to 9.

Tool-independent identity: the loaded image (loadable segments copied to their `p_vaddr`,
SHA-256) of this target is
`071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a`. It is the same whichever
SelfUtil build produced the ELF.

## Ghidra itself

- Ghidra 12 headless has no Jython: a `.py` script fails with "Ghidra was not started with
  PyGhidra". Our scripts are Java GhidraScripts.
- `support/analyzeHeadless` hardcodes `MAXMEM=2G`. That is too small for this binary; raise it
  in that file (setup in docs/GHIDRA.md).
- The Orbis loader replaces an image base of 0 by `0x1000000` by default. Pass
  `-loader-imageBase 0x400000` (`-loader-baseAddr` is ignored by this loader).
- `-loader "Orbis ELF"` is rejected by headless as an invalid loader name; leave `-loader` out,
  the Orbis loader is picked automatically for this file.
- Ghidra 12.1.4's [VEX `VSQRTSS` and `VSQRTSD` rules](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Processors/x86/data/languages/avx.sinc)
  reverse the two source roles. The low
  result must be the square root of source 2 (ModRM), with the upper lanes from source 1
  (VEX.vvvv), as specified in [Intel's instruction manual](https://cdrdv2-public.intel.com/774492/325383-sdm-vol-2abcd.pdf).
  For `vsqrtss xmm1,xmm0,xmm1`, distinct low inputs 9 and 25 must produce 5, retaining XMM0's
  upper lanes. The stock rules produce 3 and retain XMM1's upper lanes. This was checked with
  synthetic single/double probes in the x86-64 verification VM and through REA before and after
  correcting an isolated private SDK copy. The original SDK remains intact; record the modified
  SLEIGH/SLA hashes and start a fresh REA output directory when using the correction. The follow
  camera update and pitch getter contain affected instructions. Instruction coverage alone
  does not establish correct decompiler semantics.

## Calling convention

- Orbis uses the System V AMD64 ABI: integer arguments in `rdi rsi rdx rcx r8 r9`,
  floats in `xmm0-xmm7`, return in `rax:rdx` / `xmm0:xmm1`, callee-saved
  `rbx rbp r12-r15`. Variadic callers set `al` to the number of vector registers.
- Leaf functions use the 128-byte red zone below `rsp`. Ghidra's stack analysis
  can read that as unused or mis-size the frame.
- Optimized code splits and merges functions (tail calls, shared epilogues,
  cold paths in the same range). A "function" boundary in Ghidra may be wrong.
  Check the call sites before you claim a size.
- Parameters Ghidra types as `undefined8` are often pointers or floats in
  another register class. Read the call sites, not the decompiler's signature.
- Ghidra can pick a wrong calling convention for functions that pass a struct or
  `this` in an unusual register. Fix the signature before trusting the output.

## CPU

- The CPU is an AMD Jaguar: SSE up to 4.2, AVX and F16C, BMI1. No AVX2, BMI2 or
  FMA3 (*unconfirmed*). Ghidra's decompiler does not model every instruction;
  an `unaff_*` or `CONCAT` expression is a sign to read the disassembly.
- Float comparisons that look odd (`ucomiss` plus parity checks) encode NaN
  handling that the replacement has to keep.

## Imports and system calls

- Library calls are linked by NID (a hash of the symbol name), not by name.
  Ghidra shows stubs. Names come from public NID databases, not from the binary.
  *Unconfirmed for this target's import layout.*
- System calls go through the libkernel wrappers; `rax` holds the syscall number.

## Hooks

- The hook is a 14-byte absolute jump (`jmp [rip+0]` plus the target) written over the start of
  the original function. Shorter functions cannot be hooked that way.
- Nothing may jump into the first 14 bytes of a hooked function from elsewhere: check the
  references to the function's start region in Ghidra before hooking.
- A replacement calls library functions through the game's own thunks (declared with
  `RT_ORIGINAL`), not through host libraries, so whatever answers the original's calls (the
  verification harness's stubs, or the runtime) answers the replacement's the same way.

## Verification traps

- A replacement that returns the same value can still differ in memory
  writes. Write order does not matter but final bytes do: `verify.py` compares
  the final byte map.
- Uninitialized padding in structs is copied by the original. Replacement code
  that zeroes padding fails `verify.py` on memory writes; decide per case and
  record it in the function's note.

## Ghidra hides code after the engine's fatal error call

Ghidra marks the engine's fatal error function (`0x024b55b0`, "file, line, message") as
non-returning, so the bytes after a call to it are not disassembled unless something jumps there.
The game's code does continue there (a jump to the return, or to another report), which matters
when a replacement is tested with that function stubbed. If a draft's disassembly has a gap after
such a call, disassemble the bytes directly (objdump on the ELF) before writing the replacement.
Example: `kernel_condition_wait` (`0x02483e80`) returns -3 after the EINVAL report.
Since #189, `tools/draft.sh` marks each such span with `; GAP:` and decodes it. Example:
`0x01ea4a10` (#188) re-reads an entry at `0x01ea4b17` after the report and goes on to keep it.

## Replay overlapping community patch writes in order

The [community XML at commit e344639](https://github.com/shadps4-emu/ps4_cheats/blob/e34463942099072e5d8f5e2bcb3d7d927683b349/PATCHES/Bloodborne.xml)
contains overlapping `Uncap FPS++` writes inside `0x02713870`. A five-byte call write at
`0x02715d71` is partly overwritten by a later write starting at `0x02715d75`. The resulting
instruction calls `0xffffffff917166f7`; analyzing the first write alone gives the wrong target.
A still later edit at `0x02713d76` replaces the incoming stock branch with a live path that
loads `SprjFlipper::frame_seconds`, applies scalar `min` with 1/30 s, and jumps past the malformed
residual code to `0x02715d79`. Direct intraprocedural control-flow walking finds no reachable
incoming edge to the malformed call. This does not establish the absence of unknown external
jumps.

The invalid-owner route instead reads `0x0575a908`, initialized by this patch to bits
`0x3c088889` (1/120 s). A replacement must reproduce the reachable patched routes, including
their float operand order, rather than keep the stock time step because one isolated edit
looks invalid. Retain the exact XML revision and replay all its writes before reasoning about
option coverage; other revisions may differ.
