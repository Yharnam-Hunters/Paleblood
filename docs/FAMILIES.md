# Function families

The executable holds tens of thousands of tiny functions that the compiler emitted many times
over with the same bytes: an empty virtual method is a lone `ret`, a getter is
`mov rax, [rdi+K]; ret`. A **family** replaces every one of them with one readable body (one per
parameter value, such as the field offset), proved by the same differential test as a
hand-written function, run in batches.

Families make the function count move much faster than the byte count: a family member is one to
a few bytes. `tools/progress.py` reports family members on their own row in both counts and bytes
so the numbers read honestly.

## Tables

| File | Holds |
|---|---|
| `symbols/families.csv` | one row per family: name, system, exact byte shape, return class, the pre-registered member count and bytes, a note |
| `symbols/families/<family>.csv` | one row per member: address, size, extent (the member plus its NOP padding), shape parameter, status |
| `symbols/families/<family>.verification.csv` | one row per member in the `symbols/verification.csv` format: the latest `verify.py family --record` |

Members are not in `symbols/functions.csv` or `game/hooks.csv`, which keeps every table under the
repository's 2 MB file limit. A member is named `<system>_<family>_<address>` (for example
`leaf_returns_nothing_00406600`); its replacement is `bb_<system>_<family>`, or
`bb_<system>_<family>_<value>` for a shape with a parameter. `tools/gen_hooks.py` builds the
family hook table from the member tables, and `tools/validate_functions.py` checks all three
tables (CI).

## Who is a member

`tools/family_scan.py write FAMILY` reads your own dump's `eboot.elf` (read only) and the function
extents in `symbols/ghidra_functions.csv`. A function is a member only when:

- its bytes, from its start to its exported size, are exactly the family's shape (a shape is hex
  bytes and at most one parameter field: `{u8}`, `{d8}` a positive displacement, `{u32}`);
- every byte after it up to the next function is the compiler's NOP padding, and that extent is
  at least 14 bytes, so the hook's absolute jump overwrites only padding (QUIRKS.md: a hook is a
  14-byte jump; a member shorter than that is hooked across its own padding, never into the next
  function);
- no relocation writes into its extent, no community patch line (`--patches`) does, it has no row
  in `symbols/functions.csv` and no source file under `game/` names its address (a declared
  original or an observe hook).

The scan prints how many functions each rule excluded. The member count and bytes are written
into `symbols/families.csv` before the table is generated: `write` refuses a table whose numbers
differ. `family_scan.py check` re-checks every member against the executable.

Shapes are deliberately narrow. A body that reads memory the case does not control (a global, a
RIP-relative load), calls anything, or writes only part of a register (`mov al, 1` keeps the
caller's upper bytes of `rax`) is not a family: no synthetic case can stand for the value it
depends on.

## Verification

```
tools/verify.py family --family returns_nothing --jobs 10 --out summary.json [--record]
```

Every member runs its edge cases (`game/<system>/family_cases.py`) on the original and on the
replacement, compared exactly as `verify.py run` compares one function. Members are split into
shards; each shard is one harness process per side (`tools/harness.py --plan`) that maps the image
and loads the game library once, and every case still runs in its own fork. The cases seed `rax`
and the argument registers with values no replacement could guess and compare them after the call,
point every argument at a probe buffer, and compare all writable memory.

**Mutants.** On a sample of members (spread over the table, at least one per parameter value)
every mutant the family lists (`test/family_mutants.c`, built as `libbbfamily_mutants.so`; never
in the game library), and for a parameterized family the replacement of a neighbouring value, run
in place of the real body. Each must fail on every sampled member, or the run fails.

`--record` writes the per-member results and moves passing members from `replaced` to
`edge-verified`; a run with any failure or surviving mutant records nothing. `--out` is what
`tools/session_report.py --verify` reads: the report lists each changed family, and CI refuses a
family change without a clean summary.

**Verified** needs what it needs for any function (CONTRIBUTING.md): current recorded and edge
results and an approved independent review in `symbols/reviews.csv`. Until recordings exist,
members stay `edge-verified`.

## Families

| Family | Shape | What the members are |
|---|---|---|
| `returns_nothing` | `c3` | empty functions: empty virtual methods and callbacks that do nothing |
| `returns_zero` | `31 c0 c3` | functions that return false, null or 0 (all of rax) |
| `returns_constant` | `b8 {u32} c3` | functions that return a fixed number, zero-extended to rax |
| `reads_qword_field` | `48 8b 47 {d8} c3` | getters of a 64-bit field (often a pointer) K bytes into the object |
| `reads_dword_field` | `8b 47 {d8} c3` | getters of a 32-bit field, zero-extended |
| `reads_word_field` | `0f b7 47 {d8} c3` | getters of a 16-bit field, zero-extended |
| `reads_byte_field` | `0f b6 47 {d8} c3` | getters of an 8-bit field, zero-extended (`mov al, [rdi+K]` is excluded) |
| `field_address` | `48 8d 47 {d8} c3` | the address of a member or base-class part, K bytes into the object |
| `reads_first_qword` | `48 8b 07 c3` | getters of the 64-bit field at the start of the object |
| `reads_first_dword` | `8b 07 c3` | getters of the 32-bit field at the start of the object |
| `writes_qword_field` | `48 89 77 {d8} c3` | setters of a 64-bit field from the second argument |
| `writes_dword_field` | `89 77 {d8} c3` | setters of a 32-bit field |
| `writes_word_field` | `66 89 77 {d8} c3` | setters of a 16-bit field |
| `writes_byte_field` | `40 88 77 {d8} c3` | setters of an 8-bit field (a bool or small enum) |
| `writes_first_qword` | `48 89 37 c3` | setters of the 64-bit field at the start of the object |
| `writes_first_dword` | `89 37 c3` | setters of the 32-bit field at the start of the object |
| `returns_first_argument` | `48 89 f8 c3` | identity functions: return the first argument |
| `returns_second_argument` | `48 89 f0 c3` | return the second argument |

A parameterized family's source is generated by `tools/family_emit.py FAMILY` from its member
table and a template (one named constant and one replacement per value); the validator refuses a
source that differs from the generator's output.
