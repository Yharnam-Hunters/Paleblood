<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Engine

The engine's shared plumbing that every system leans on: its fatal error report, its check for missing singletons, its reference-counted objects, and small helpers such as writing a line to the standard error stream.

**At a glance**

- **Scope:** Engine-wide helpers in Paleblood's `game/engine/`: the fatal error report and singleton check, reference counting, and replaced engine utility functions.
- **Verified:** `engine_write_stderr`, the string helpers (comparisons, appends, case changes), wide-string set-up, owner storage, a word-and-product store and a handle-table lookup are edge-verified against constructed cases.
- **Open:** Who calls the stderr writer during play, and with what text.

## Summary

### What every system shares

Most replaced functions lean on a few engine-wide pieces:

- **The fatal error report** (`0x024b55b0`) takes a source file, a line and a message. In the
  shipped game it logs and *returns*, so the code after it keeps running; Ghidra wrongly treats it
  as never returning (see [Ghidra & PS4 Quirks](../../QUIRKS.md)).
- **The singleton check.** Many systems are singletons (`FrpgNetMan`, `SprjEventFlagMan`, ...).
  Code that needs one reports it by name through the fatal error report when it is missing, then
  reads it again and uses whatever is there.
- **Reference-counted objects** (`DLReferenceCountObject`). An object counts its holders; dropping
  the last reference destroys it through its first virtual function, and dropping one more than
  it has is reported as an invalid unref. The render frame update and the Chalice Dungeon gate's
  handle share this code ([#185](https://github.com/Yharnam-Hunters/Paleblood/pull/185)).

### Writing to standard error

`engine_write_stderr` (`0x0111a740`) writes a text to the standard error stream: it reads the C
library's `stderr` once, passes the text to `fputs`, ignores the result, and returns 0.

<details>
<summary>How the stderr writer was verified</summary>

By [@evill33t](https://github.com/evill33t) in #113, brought up to date with `main` and merged
in [#205](https://github.com/Yharnam-Hunters/Paleblood/pull/205). Its 28 constructed edge cases
pass against the original on current `main`, and an independent review approved it; no frame-rate
patch edits it. It has not been checked on frames recorded from a real game run.

</details>

### Comparing strings

Two byte-string comparisons compare signed bytes and return the difference at the first mismatch:
`engine_compare_signed_strings` (`0x011c4970`) runs to the end of the strings, and
`engine_compare_signed_prefix` (`0x011c4990`) stops after a length, still comparing the first byte
when the length is zero or negative. All their callers are in FMOD's code (see [Audio](audio.md)).

### More string helpers

Since 4 November the engine's small C-string helpers are mapped too, by [@evill33t](https://github.com/evill33t):
`engine_string_length` (`0x011c47f0`, a `strlen` whose result is cut to 32 bits,
[#288](https://github.com/Yharnam-Hunters/Paleblood/pull/288)), `engine_ascii_to_upper_in_place` and `engine_ascii_to_lower_in_place`
(`0x011c4910`, `0x011c4940`: change letters in place and return the string, [#281](https://github.com/Yharnam-Hunters/Paleblood/pull/281)), and
`engine_compare_ascii_ignoring_case` (`0x011c49c0`, compares two strings ignoring letter case,
[#282](https://github.com/Yharnam-Hunters/Paleblood/pull/282)).

Three more followed on 5 and 6 November, also by [@evill33t](https://github.com/evill33t):

- `engine_string_append` (`0x011c4870`) appends one zero-terminated string to another, a `strcat`
  that returns the destination ([#337](https://github.com/Yharnam-Hunters/Paleblood/pull/337), from #118);
- `engine_string_append_counted` (`0x011c48b0`) appends at most a given number of bytes, counting
  only the low 32 bits of the count, and ends the result with a zero; a count of zero touches
  neither string ([#337](https://github.com/Yharnam-Hunters/Paleblood/pull/337));
- `engine_ascii_to_lower` (`0x011c48f0`) lowercases a single character: it changes `A`-`Z` to
  `a`-`z`, leaves every other byte as it is, and returns the byte as a signed 32-bit value
  ([#125](https://github.com/Yharnam-Hunters/Paleblood/pull/125)).

### Wide strings and owned storage

- `engine_wide_string_destroy` (`0x02a44140`) releases a wide (16-bit character) string, and
  `engine_initialize_static_wide_string_0217ffb0` sets up one static wide string the first time it
  is needed, guarded so it happens once ([#284](https://github.com/Yharnam-Hunters/Paleblood/pull/284)).
- `engine_release_owner_storage` and `engine_resize_owner_storage` (`0x0132c590`, `0x0132c6c0`)
  free an object's storage block through its allocator, or replace it with a new 16-byte-aligned
  block of the requested size ([#285](https://github.com/Yharnam-Hunters/Paleblood/pull/285)). What kind of object owns the block is not known yet;
  its only known caller is `0x02713870`.

### Storing a word and a product

`engine_store_word_product` (`0x011c4510`) stores two 32-bit fields of an object: one input as it
is at offset `0x18`, and the product of two other inputs times four at offset `0x1c`, then
returns 0 ([#336](https://github.com/Yharnam-Hunters/Paleblood/pull/336), from #115 by [@evill33t](https://github.com/evill33t)). What the two fields mean is not known yet;
it is called from `0x01111bb0` and through slot 1 of the function table at `0x0500ae48`.

### Looking up a table entry by handle

`engine_entry_manager_resolve` (`0x01314320`) turns a handle into an entry: it checks that the
table is enabled, rejects the all-ones handle, uses the handle's low 32 bits as an index checked
against the table's capacity, and rejects an empty entry. Its callers are a family of functions
at `0x02717f30`-`0x02718c80` and `0x0210f5a0`; what the table holds is not known yet.

<details>
<summary>How these were verified</summary>

By [@evill33t](https://github.com/evill33t): the comparisons in #107, merged in
[#274](https://github.com/Yharnam-Hunters/Paleblood/pull/274) (18 and 24 cases), and the lookup
in #110, merged in [#276](https://github.com/Yharnam-Hunters/Paleblood/pull/276) (25 cases).
Each was brought up to date with `main`, its constructed edge cases pass against the original
on current `main`, and an independent review approved it. The appends ([#337](https://github.com/Yharnam-Hunters/Paleblood/pull/337), 32 and
48 cases), the word-and-product store ([#336](https://github.com/Yharnam-Hunters/Paleblood/pull/336), 32 cases) and the single-character
lowercase ([#125](https://github.com/Yharnam-Hunters/Paleblood/pull/125), 291 generated cases) are edge-verified: their constructed
cases pass against the original. None was checked on recorded game runs.

</details>

### Vector and rotation math

The wind update of [#568](https://github.com/Yharnam-Hunters/Paleblood/pull/568) (see [Render](render.md#wind-on-models)) inlines the engine's four-lane
vector math, now in `game/engine/rotation_math.h`: a sine and cosine polynomial evaluated on four
values at once, a rotation quaternion built from half-angles, the rotation matrix made from it, and
a vector length refined with a Newton step. `vector_operations.h` gained additions and
multiplications that keep the original's operand order, so NaN results carry the same payloads.
They are helpers of that replacement, not separate functions of the executable.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0111a740` | 20 | `engine_write_stderr` | edge-verified | Forward the string and imported _Stderr object address to fputs; ignore its return and return zero; synthetic wire edge cases only |
| `0x011c4510` | 15 | `engine_store_word_product` | edge-verified | Store low 32-bit input at owner+0x18 and modulo-32-bit product times four at owner+0x1c; valid unaligned byte view and full zero return; field meanings unknown |
| `0x011c47f0` | 29 | `engine_string_length` | edge-verified | Engine strlen; scan to the first NUL and return the low 32 length bits zero-extended into RAX; other incoming GP registers are ignored |
| `0x011c4870` | 51 | `engine_string_append` | edge-verified | NUL-terminated string append returns original destination; includes prefixes beyond four bytes and finite aliases |
| `0x011c48b0` | 60 | `engine_string_append_counted` | edge-verified | Bounded string append uses low32 count; zero count touches neither pointer; appends NUL after count bytes when needed |
| `0x011c48f0` | 18 | `engine_ascii_to_lower` | edge-verified | ASCII lowercase of the low input byte; sign-extend the result byte to 32 bits; the upper 32 bits of RAX are zero |
| `0x011c4910` | 44 | `engine_ascii_to_upper_in_place` | edge-verified | Convert ASCII a-z to A-Z in place through NUL; retain all other bytes and return original pointer; caller 0x01127ca0 |
| `0x011c4940` | 44 | `engine_ascii_to_lower_in_place` | edge-verified | Convert ASCII A-Z to a-z in place through NUL; retain all other bytes and return original pointer; caller 0x01127ca0 |
| `0x011c4970` | 29 | `engine_compare_signed_strings` | edge-verified | Compare zero-terminated strings by signed bytes and return their first differing byte subtraction |
| `0x011c4990` | 47 | `engine_compare_signed_prefix` | edge-verified | Compare signed bytes up to a signed 32-bit bound; always compare the first byte even for zero or negative lengths |
| `0x011c49c0` | 56 | `engine_compare_ascii_ignoring_case` | edge-verified | Case-insensitive string comparison folds ASCII uppercase bytes then returns signed-byte difference as int32; bounded aliases covered |
| `0x01314320` | 54 | `engine_entry_manager_resolve` | edge-verified | Resolve an enabled low32 handle index with unsigned bounds; reject full-width handle and entry sentinels; return opaque table pointers unchanged |
| `0x0132c590` | 49 | `engine_release_owner_storage` | edge-verified | Free nonnull owner storage through its allocator before clearing the field; wider role unknown; caller 0x02713870 |
| `0x0132c6c0` | 81 | `engine_resize_owner_storage` | edge-verified | Free old owner storage before clearing; reload allocator after callback and allocate requested low32 byte count aligned to 16; caller 0x02713870 |
| `0x0217ffb0` | 205 | `engine_initialize_static_wide_string_0217ffb0` | edge-verified | Guarded static empty wide string; byte-only entry test; captured allocator; exact partial stores; destructor registration before full guard publication |
| `0x02a44140` | 59 | `engine_wide_string_destroy` | edge-verified | WideString destructor; unsigned capacity >=8 frees even null; ignores owns; callback before capacity and size reset and two-byte terminator |
