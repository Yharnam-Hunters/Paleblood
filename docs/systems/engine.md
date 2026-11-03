<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Engine

The engine's shared plumbing that every system leans on: its fatal error report, its check for missing singletons, its reference-counted objects, and small helpers such as writing a line to the standard error stream.

**At a glance**

- **Scope:** Engine-wide helpers in Paleblood's `game/engine/`: the fatal error report and singleton check, reference counting, and replaced engine utility functions.
- **Verified:** `engine_write_stderr` (`0x0111a740`) is edge-verified against constructed cases.
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

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0111a740` | 20 | `engine_write_stderr` | edge-verified | Forward the string and imported _Stderr object address to fputs; ignore its return and return zero; synthetic wire edge cases only |
