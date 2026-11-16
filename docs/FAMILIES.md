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
| `symbols/families/<family>.matching.csv` | one row per member in the `symbols/matching.csv` format: the latest `match.py family --record` ([MATCHING.md](MATCHING.md), "Families") |
| `symbols/families/<family>.reviews.csv` | one row per reviewed member in the `symbols/reviews.csv` format (a family's rows would overflow `symbols/reviews.csv`) |

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
  bytes and at most one parameter field: `{u8}`, `{d8}` a positive displacement, `{u32}`, `{d32}` a positive 32-bit displacement);
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

### Relative shapes

The one exception is a **relative shape**: copies of one larger function that the compiler
emitted at many addresses (an inline function's guarded static initializer, say), which differ
only in their rip-relative and call displacements because they sit at different addresses. A
relative shape is hex bytes and `{rel:TARGET}` fields, no parameter field:

- `{rel:0x05acebc8}` is a 4-byte displacement (a rip-relative operand, or a call or jump out of
  the function) that must reach `0x05acebc8` from the end of the field;
- `{rel:0x05acebc8+1}` reaches it from that many bytes after the field, for an instruction that
  ends with an immediate after its displacement (`cmp byte ptr [rip+d], 0`).

A member's bytes outside the fields are exactly the shape's, and every field, at the member's own
address, reaches its target: every member is the same code reaching the same globals and the same
functions. Jumps inside the function stay plain bytes (they are the same wherever the copy is).
The other rules above (padding, extent, relocations, patch lines, named functions) apply
unchanged, and `family_scan.py check` re-checks each field of every member.

`family_scan.py relative-shape ADDRESS` writes the shape of one function from its disassembly
(needs Capstone), and checks that it matches the function itself. The cases of a relative family
control the globals and stub the calls the shape reaches, as a hand replacement's cases do, so
they stand for every member. Its replacement is usually a thin entry point over the hand
replacement of one copy, named so that the family's source hash covers that body
(`bb_<system>_<family>` and the hand replacement `bb_<system>_<family>_<address>` share the
family's name). Its members are as large as that function, so the recorder's hook fits in them:
they reach verified through recordings like a hand replacement, and a relative family has no
matching template in `tools/family_emit.py`.

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

**Verified** needs what it needs for any function (CONTRIBUTING.md): a current, passing edge
result, current recorded results **or** a current byte-exact match with `targets_checked=yes`, and
an approved independent review. Recordings cannot be made for most members: the recorder's hook
needs room to relocate the function's first instructions, and a member is one to six bytes
followed by padding. So members use the matching path ([MATCHING.md](MATCHING.md), "Families"):
`tools/match.py family FAMILY --record` compares every member with the family's generated
matching source, and the independent review of the family writes one row per member it approves
in `symbols/families/<family>.reviews.csv` (its notes say "family review"). Then

```
tools/promote.py --families
```

moves each `edge-verified` member with all three to `verified`. It runs nothing: it reads the
recorded evidence, and `tools/validate_functions.py` applies the same rule in CI.

## Families

| Family | Shape | What the members are |
|---|---|---|
| `returns_nothing` | `c3` | empty functions: empty virtual methods and callbacks that do nothing |
| `zeroes_record_and_stores_first_argument` | 126 exact bytes (`symbols/families.csv`) | record initializers: clear 15 words at the second argument, store the first after them, return 0 |
| `returns_zero` | `31 c0 c3` | functions that return false, null or 0 (all of rax) |
| `returns_constant` | `b8 {u32} c3` | functions that return a fixed number, zero-extended to rax |
| `matches_sixteen_byte_key` | 45 exact bytes | key matches: the size argument is 16 and the key equals the 16 bytes at +0x18 |
| `counts_list_nodes` | 28 exact bytes | the length of the singly linked list at +0x30 (next pointer at +0x10) |
| `reads_nth_list_node_dword` | 34 exact bytes | the dword at +0x18 of the nth node of the list at +0x30, or 0 |
| `reads_qword_field` | `48 8b 47 {d8} c3` | getters of a 64-bit field (often a pointer) K bytes into the object |
| `reads_far_qword_field` | `48 8b 87 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `reads_dword_field` | `8b 47 {d8} c3` | getters of a 32-bit field, zero-extended |
| `reads_far_dword_field` | `8b 87 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `reads_word_field` | `0f b7 47 {d8} c3` | getters of a 16-bit field, zero-extended |
| `reads_far_word_field` | `0f b7 87 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `reads_byte_field` | `0f b6 47 {d8} c3` | getters of an 8-bit field, zero-extended (`mov al, [rdi+K]` is excluded) |
| `reads_far_byte_field` | `0f b6 87 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `field_address` | `48 8d 47 {d8} c3` | the address of a member or base-class part, K bytes into the object |
| `far_field_address` | `48 8d 87 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `reads_first_qword` | `48 8b 07 c3` | getters of the 64-bit field at the start of the object |
| `reads_first_dword` | `8b 07 c3` | getters of the 32-bit field at the start of the object |
| `writes_qword_field` | `48 89 77 {d8} c3` | setters of a 64-bit field from the second argument |
| `writes_far_qword_field` | `48 89 b7 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `writes_dword_field` | `89 77 {d8} c3` | setters of a 32-bit field |
| `writes_far_dword_field` | `89 b7 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `writes_word_field` | `66 89 77 {d8} c3` | setters of a 16-bit field |
| `writes_far_word_field` | `66 89 b7 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `writes_byte_field` | `40 88 77 {d8} c3` | setters of an 8-bit field (a bool or small enum) |
| `writes_far_byte_field` | `40 88 b7 {d32} c3` | the same, with the field 0x80 bytes or more into the object |
| `writes_first_qword` | `48 89 37 c3` | setters of the 64-bit field at the start of the object |
| `writes_first_dword` | `89 37 c3` | setters of the 32-bit field at the start of the object |
| `returns_first_argument` | `48 89 f8 c3` | identity functions: return the first argument |
| `stores_constant_in_first_dword_returning_object` | `c7 07 {u32} 48 89 f8 c3` | store a constant (a type or kind tag) in the first dword and return the object |
| `reads_dword_through_pointer_at_8` | `48 8b 47 08 8b 40 {d8} c3` | the dword K bytes into the object that the pointer at +8 points at |
| `reads_qword_through_pointer_at_8` | `48 8b 47 08 48 8b 40 {d8} c3` | the qword K bytes into the object that the pointer at +8 points at |
| `address_through_pointer_at_8` | `48 8b 47 08 48 83 c0 {d8} c3` | the address K bytes into the object that the pointer at +8 points at |
| `returns_all_bits` | 8 exact bytes | functions that return -1 (all 64 bits set) |
| `resets_first_qword_returning_object` | 11 exact bytes | clear the first qword and return the object |
| `adjusts_pointer_at_8_unless_null` | 22 exact bytes | the pointer at +8 plus 0xd0 (a part of the object it points at), or null |
| `reads_wide_string_text_at_0x30` | 19 exact bytes | the text of the wide string the pointer at +0x30 points at (inline below capacity 8) |
| `writes_top_four_bits_of_first_dword` | 15 exact bytes | set bits 28..31 of the first dword |
| `writes_low_ten_bits_of_first_dword` | 18 exact bytes | set bits 0..9 of the first dword |
| `packs_first_dword_from_two_bit_fields` | 14 exact bytes | set the first dword to two bit fields (bits 0..9 and 28..31), the rest clear |
| `returns_second_argument` | `48 89 f0 c3` | return the second argument |
| `atomically_increments_dword_at_8` | `b8 01 00 00 00 f0 0f c1 47 08 c3` | atomic increments of the 32-bit count 8 bytes into the object, returning the old count |
| `reads_pointer_without_low_bit` | `48 8b 47 {d8} 48 83 e0 fe c3` | getters of a 64-bit field with its low bit (a tag) cleared |
| `clears_first_two_qwords` | 16 exact bytes | clear the first 16 bytes of the object |
| `clears_first_four_qwords` | 32 exact bytes | clear the first 32 bytes of the object |
| `clears_two_qwords_and_a_dword` | 23 exact bytes | clear the first 20 bytes of the object (two qwords and a dword) |
| `returns_float_zero` | `c5 f8 57 c0 c3` | functions that return 0.0f (all of xmm0 zero) |
| `initialize_static_wide_string` | relative, 205 bytes (0x0217ffb0's) | copies of the guarded static initializer of one empty wide string (`engine/static_wide_string_init.cpp`) |
| `destroys_holder_of_shared_work` | relative, 128 bytes (0x01e64140's), 4 `{rel:...}` | in-place destructor of a holder of shared work (two more are in `menu/scaleform_glue.cpp`) |
| `destroys_owner_of_wide_string_then_base_0x0139b630` | relative, 135 bytes (0x018d7d10's), 3 `{rel:...}` | destructor of an object owning a heap wide string (+0x30, flag +0x44), then the base destructor |
| `deletes_owner_of_wide_string_after_base_0x0139b630` | relative, 143 bytes (0x018d7da0's), 4 `{rel:...}` | the deleting form of the same destructor |
| `visits_children_of_list_at_0x40` | relative, 159 bytes (0x02e12d30's), 2 `{rel:...}` | walk the children of the list at +0x40 through 0x00b69e20 and the own slot 0x28 |
| `secondary_base_visits_children_of_list_at_0x40` | relative, 159 bytes (0x02e13170's), 2 `{rel:...}` | the same walk entered through the secondary base at +0x48 |
| `selects_index_of_changed_id_with_0x01348fe0` | relative, 103 bytes (0x0260f7f0's), one `{rel:...}` | when a followed id changes, pass its table index to 0x01348fe0 with the owner's target |

A parameterized family's source is generated by `tools/family_emit.py FAMILY` from its member
table and a template (one named constant and one replacement per value); the validator refuses a
source that differs from the generator's output.
