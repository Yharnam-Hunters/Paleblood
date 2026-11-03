<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Engine

The engine's shared plumbing that every system leans on: its fatal error report, its check for missing singletons, its reference-counted objects, and small helpers such as writing a line to the standard error stream.

**At a glance**

- **Scope:** Engine-wide helpers in Paleblood's `game/engine/`: the fatal error report and singleton check, reference counting, and replaced engine utility functions.
- **Verified:** `engine_write_stderr`, two string comparisons and a handle-table lookup are edge-verified against constructed cases.
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
on current `main`, and an independent review approved it.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0111a740` | 20 | `engine_write_stderr` | edge-verified | Forward the string and imported _Stderr object address to fputs; ignore its return and return zero; synthetic wire edge cases only |
| `0x011c4970` | 29 | `engine_compare_signed_strings` | edge-verified | Compare zero-terminated strings by signed bytes and return their first differing byte subtraction |
| `0x011c4990` | 47 | `engine_compare_signed_prefix` | edge-verified | Compare signed bytes up to a signed 32-bit bound; always compare the first byte even for zero or negative lengths |
| `0x01314320` | 54 | `engine_entry_manager_resolve` | edge-verified | Resolve an enabled low32 handle index with unsigned bounds; reject full-width handle and entry sentinels; return opaque table pointers unchanged |
