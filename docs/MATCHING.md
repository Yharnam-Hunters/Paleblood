# Matching

`tools/match.py` rebuilds a function from C source with the public compiler of the game's era and
compares the result with the original machine code. A byte-exact match shows that the source
compiles to the same instructions as the original, for every input. That makes it strong evidence
for small C-style functions, where edge cases only sample the inputs.

Matching is **optional supporting evidence**. It never replaces `tools/verify.py`
([VERIFY.md](VERIFY.md)) or the independent review, and it is never a gate. Like `verify.py`, it
runs only on your own machine, against your own dump: CI has no game bytes, so CI checks only the
format of the record.

## The compiler

The PS4 SDK's CPU compiler is Sony's fork of Clang/LLVM. **That compiler is proprietary: never
obtain or use it.** Its public facts are enough to reproduce its output with upstream clang:

- Sony's driver defaults: `-fPIC -fstack-protector-strong -fno-omit-frame-pointer
  -momit-leaf-frame-pointer -fno-exceptions -fno-rtti -std=gnu++11`. Source: Paul T. Robinson,
  ["Developer Toolchain for the PlayStation 4"](https://llvm.org/devmtg/2013-11/slides/Robinson-PS4Toolchain.pdf),
  LLVM Developers' Meeting, November 2013. The same talk says SDK releases are based on upstream
  LLVM releases.
- The CPU is AMD Jaguar, `-march=btver2`. Source: Greg Bedwell,
  ["PS4 CPU toolchain"](https://llvm.org/devmtg/2014-04/PDFs/Talks/GBedwell_PS4CPUToolchain_EuroLLVM2014_distribution.pdf),
  EuroLLVM 2014.
- The executable agrees with both. Non-leaf functions keep the frame pointer and leaf functions
  omit it, functions with address-taken locals check the stack canary, and BMI1 and VEX
  instructions appear. Its process parameters declare SDK 2.00.
- Of the upstream releases tried (3.3 to 3.9, and current clang), **clang 3.4.2** reproduces the
  most functions byte for byte: 28 of 33 small functions in the first experiment, against 19 for
  3.5.2 and 16 for 3.3.

Clang 3.4 has no `-fstack-protector-strong`; Sony carried it as a downstream patch, and it reached
upstream in 3.5. So `tools/match.py` has two flag sets:

| flags | adds | use for |
|---|---|---|
| `ps4-o2` | `-fstack-protector` | almost everything: like `-strong`, it leaves functions without arrays or address-taken locals alone |
| `ps4-o2-sspall` | `-fstack-protector-all` | a function whose original checks the stack canary (`-strong` would protect it) |

Both use `-target x86_64-unknown-freebsd -march=btver2 -O2 -fPIC -fno-omit-frame-pointer
-momit-leaf-frame-pointer -fno-exceptions`. C sources add `-std=gnu99`; C++ sources add
`-std=gnu++11 -fno-rtti`. FreeBSD is the closest upstream target: `x86_64-scei-ps4` only arrived
in clang 3.7.

Some functions stay just short of exact. Sony's compiler is a fork near 3.4 with its own patches
(one function in the first experiment matched only with 3.3), so a few block layouts, `cmov`
choices and scheduling details cannot be reached with any one upstream release. A near-match is
still useful information. Don't chase it.

## Setting up

You need Linux on x86-64, Python 3.12, binutils (`objdump`) and your dump's `eboot.elf`
(`tools/prepare_dump.sh`, [ONBOARDING.md](ONBOARDING.md)).

```
tools/match.py toolchain --fetch
```

This downloads `clang+llvm-3.4.2-x86_64-linux-gnu-ubuntu-14.04.xz` from llvm.org's release page,
checks its SHA-256 (`ef0d8faeb31c731b46ef5859855766ec7eb71f9a32cc6407ac5ddb4ccc35c3dc`), and
unpacks only `clang` and its headers into `~/.cache/paleblood/toolchains/clang-3.4.2`
(`$BB_MATCH_TOOLCHAINS` changes the directory, and `$BB_MATCH_CLANG` points at a clang 3.4.2 you
already have). Nothing is vendored into the repository. The 2014 binary links `libtinfo.so.5`;
where that is missing, the tool points it at ncurses 6, which provides the same symbols.

**objdiff (optional).** For a second opinion, and for its interactive view, build
[objdiff](https://github.com/encounter/objdiff)'s command-line tool (it needs Rust):

```
git clone https://github.com/encounter/objdiff && git -C objdiff checkout v3.8.3
cargo build --release --manifest-path objdiff/Cargo.toml -p objdiff-cli
```

Then pass `--objdiff objdiff/target/release/objdiff-cli`, or set `BB_OBJDIFF`.

## Running

```
tools/match.py run engine_string_length --source game/matching/engine/string.c
tools/match.py run engine_string_length --diff        # side by side, while tuning
tools/match.py run engine_string_length --record      # write the row
tools/match.py all                                    # re-run every recorded row
```

`run` compiles the source, cuts the function out of your dump (its address and size come from
`symbols/functions.csv`) and compares the two:

- **byte_exact**: every byte is equal, except the 4-byte fields the compiler leaves to the linker
  (relocations: calls, tail calls, references to globals), **and** every one of those fields goes
  to the same place as the original's (below). One linker decision is also allowed: a GOT load
  (`mov reg, [rip+sym@GOTPCREL]`) that the linker relaxed to `lea` for a symbol defined in the
  executable.
- **targets_checked**: `yes` when each reference was resolved on both sides and found equal.
  A byte-exact row always has it; the column keeps rows recorded before the check apart.
- **match_percent**: how similar the two instruction sequences are. Branch targets inside the
  function become offsets, and references outside it become placeholders. It is 100.0 only for
  a byte-exact match.
- **objdiff_percent**, with `--objdiff`: objdiff's own score. The tool rebuilds the original
  function as a relocatable object with its external references as relocations, so objdiff
  compares like with like. objdiff cannot know about the GOT relaxation, so a byte-exact function
  that reads a global can score below 100 there.

### Where the references go

Masking the relocation fields alone would accept a call to the wrong function or a load of the
wrong global. So for every relocation in our object, `match.py` works out the place it means and
the place the original's field at the same offset reaches, and requires them to be the same:

| our reference | means | the original's field |
|---|---|---|
| an undefined name in `symbols/functions.csv` | that function's address | a call or jump's target; a RIP-relative address |
| an undefined name of a function family member (`leaf_reads_first_dword_013e9ea0`, [FAMILIES.md](FAMILIES.md)) | that member's address | the same |
| an undefined name declared by a replacement with `RT_ORIGINAL` or `RT_GLOBAL` | that address | the same, or the GOT slot a `mov` reads (what the loader writes there), or the address a relaxed `lea` takes |
| any other undefined name | an import: the PS4 name id (NID) of the name | a call through a PLT stub, or a GOT slot, that the loader fills with that NID |
| our own read-only data (string, constant pool, switch or jump table) | its bytes | the bytes there, compared in full; a jump table's entries are resolved like the code's references |

A name the repository gives one function and a different one elsewhere is refused as ambiguous.
Writable data defined in the matching source is refused: equal starting values do not make two
globals the same, so declare it `extern` with its `RT_GLOBAL` name. Matching sources never vouch
for their own names: only `symbols/functions.csv`, the family member tables
(`symbols/families/<family>.csv`) and the `RT_ORIGINAL` / `RT_GLOBAL` declarations outside
`game/matching/` count. Each reference that cannot be shown equal is printed
(`target: ...`) and the function is not byte-exact. The fix is either a wrong callee in the source,
or a missing name: use the name the repository already has for that address, or give it one in
the replacement that uses it.

The objects are built in a temporary directory and removed afterwards. `--diff` prints both
instruction sequences to your terminal: that is the game's disassembly. Keep it on your machine,
like your dump. Never paste it into an issue, a pull request or a commit.

Exit codes: 0 byte-exact, 1 not byte-exact, 3 bad input.

## Writing matching source

- **Clean room.** Write the C from your own understanding of the function: its behaviour, its
  verified replacement, its edge cases. Then tune the shape (types, loop form, statement order)
  until the compiler agrees. Never paste decompiler output.
- **Where:** `game/matching/<system>/<topic>.c`, one file per topic, like the replacements. The
  game library does not build these files; CI parses them with a current C compiler, and
  `tools/check_readable.py` checks them like any other game source.
- **Language:** C (gnu99), or C++11 in a `.cpp` file. Clang 3.4 knows nothing newer. C++
  functions are declared `extern "C"` so their names are not mangled.
- **Names:** the C function has the `symbols/functions.csv` name (`engine_string_length`, not
  `bb_engine_string_length`). Calls to other game functions and uses of game globals are plain
  `extern` declarations with the repository's name for them (`symbols/functions.csv`, a family
  member's name such as `leaf_reads_byte_field_013e9eb0`, or the replacement's `RT_ORIGINAL` /
  `RT_GLOBAL`), and system calls use the library's own name
  (`sceKernelUsleep`, `sceGnmDrawInitDefaultHardwareState200`), so the reference check can
  resolve them. Headers are clang's own (`stdint.h`, `stddef.h`, `stdbool.h`): the
  system's include directories are not searched.
- **Readable**, by the same rules as the replacements ([STYLE.md](../STYLE.md)): structs with
  named fields, named constants (an `enum` costs nothing at `-O2`) and a comment that says what
  the function does. Matching source is often more readable than the replacement, because it
  has the original's natural types back: `char` and `int` where the replacement spells out
  `uint64_t` returns and truncations.
- **Type facts.** A match is evidence about types: if a function matches only with a `bool`
  parameter or a `char` loop, say so in a comment. That fact feeds
  [TYPE_INTELLIGENCE.md](TYPE_INTELLIGENCE.md).
- **Replacements.** Where matched C is as clear as the replacement, it may become the
  replacement: one C file built by the game library (as `bb_<name>`) and matched under the
  `symbols/functions.csv` name. Keep any ABI hardening the modern build needs (explicit
  truncation, unaligned access) in a thin wrapper around the matched body.

Leaf and C-style functions up to about 100 bytes usually match within one or two attempts. Large
C++ engine functions need class layouts, inlined members and templates reconstructed. Don't
start matching them; it is not worth it yet.

## The record: `symbols/matching.csv`

One row per function, written by `tools/match.py run --record`. It holds numbers only, never
bytes or disassembly.

| column | means |
|---|---|
| `address`, `name` | the function, as in `symbols/functions.csv` |
| `compiler` | `clang-3.4.2` |
| `flags` | the flag set (`ps4-o2`, `ps4-o2-sspall`) |
| `match_percent` | instruction similarity, one decimal; `100.0` only when byte-exact |
| `objdiff_percent` | objdiff's score when it was run, otherwise empty |
| `byte_exact` | `yes` or `no` |
| `targets_checked` | `yes` when every external reference was shown to reach the same place as the original's, else `no` |
| `source`, `source_sha256` | the matching source under `game/`, and a hash of it and the headers it includes with quotes |
| `date` | the day of the run |

`tools/validate_functions.py` (pre-commit and CI) checks the format, including that a byte-exact
row has `targets_checked=yes`. It also checks that each row names a function in
`symbols/functions.csv`, that the source exists and defines that function,
and that the source has not changed since it was matched. A changed source needs `--record`
again.

## Families

Function family members ([FAMILIES.md](FAMILIES.md)) are tiny template instances, thousands of
them byte-identical, and cannot be recorded in the game. They are matched in bulk:

```
tools/family_emit.py reads_qword_field --matching      # writes game/matching/leaf/reads_qword_field.c
tools/match.py family reads_qword_field --record       # or: tools/match.py family all --record
```

- **The source** is generated, like a parameterized family's replacement: `tools/family_emit.py
  FAMILY --matching` writes `game/matching/<system>/<family>.c` from the family's template
  (`MATCHING_TEMPLATES`) and the values in its member table. It holds one C function per value,
  named like the replacement without `bb_` (`leaf_reads_qword_field_0x8`, or `leaf_returns_nothing`
  for a family without a parameter). `tools/validate_functions.py` refuses a matching source that
  differs from the generator's output.
- **The comparison** compiles that source once and compares the function of each value with
  **every member** that has that value, each against its own bytes in your dump. A member has no
  relocations (the scanner excludes them), so a compiled function that keeps any relocation is not
  byte-exact; one with none has nothing left to resolve, and its row has `targets_checked=yes`.
- **The record** is `symbols/families/<family>.matching.csv`: one row per member, with
  `symbols/matching.csv`'s columns (`name` is the member's name, `objdiff_percent` empty, and
  `source_sha256` the source's hash bound to the member's value, so a row also goes stale when
  the member's value changes).
  `tools/validate_functions.py` checks its format, that every row is a member under its own name,
  that the source is the family's generated matching source, and that the source has not changed
  since it was matched.

A current byte-exact row counts for a member exactly as it does for a named function (below): it
stands in for the recorded cases, and the member still needs its current edge result
(`verify.py family --record`) and an approved independent review, which for a family goes in
`symbols/families/<family>.reviews.csv`. The review covers the whole family once: the template
and generator, the matching source against the replacement, the per-member match and edge
evidence, and readability; it writes one row per member it approves. `tools/promote.py --families`
then promotes ([FAMILIES.md](FAMILIES.md)).

## What a match counts for

- **Below 100%:** information only. A large drop usually means a semantic difference, so it is a
  useful check on a transcription.
- **Byte-exact**, on a small C-style function: strong evidence toward `verified`. A current
  byte-exact row with `targets_checked=yes` stands in for the recorded cases that `verified`
  otherwise needs: the instructions are the original's, and so is every function, global and
  import they reach. Until the
  runtime runs the game, those recordings often cannot be made at all. The rest of the rule does
  not change. The function still needs a current, passing edge result for the shipped
  replacement (`tools/verify.py run --record edge`) and an approved independent review in
  `symbols/reviews.csv`. The review also checks that the matching source and the replacement do
  the same thing. `tools/validate_functions.py` enforces all three.
- A match is evidence about the matching source. Paleblood ships the replacement, built with a
  current compiler. That is why the edge results for the replacement stay required, unless the
  replacement *is* the matched source.
