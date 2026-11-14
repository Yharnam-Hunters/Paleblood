# Ghidra setup

How Ghidra is set up for the analysis exports, and how the denominator
(`symbols/ghidra_functions.csv`) is produced: by `tools/function_bounds.py` from the executable's
own tables ([The function list](#the-function-list)), no longer from Ghidra's function list. Keep
the Ghidra project outside the repository: it holds the game's code.

## Versions

- Ghidra 12.0.3 (`ghidra_12.0.3_PUBLIC_20260210.zip`, SHA-256
  `90d3fffb20b00030dcef8d2a24dd0f422d3a61e432b3ad43f77233ac6d667981` from the release notes).
- GhidraOrbis release 1.0144, the build for Ghidra 12.0.3
  (<https://github.com/astrelsky/GhidraOrbis>). It loads Orbis ELFs and resolves NID imports
  from its NID database. Its releases target Ghidra 12.0.x; newer Ghidra needs a matching
  GhidraOrbis build.
- A JDK 21 or newer.

## Install

1. Unzip Ghidra. Unzip the GhidraOrbis zip into `Ghidra/Extensions/` of the install.
2. In `support/analyzeHeadless` set `MAXMEM` to well above the default `2G`.

## Import, analyze, export

```
analyzeHeadless PROJECT_DIR bb_eboot -import /path/to/eboot.elf \
    -loader-imageBase 0x400000 \
    -scriptPath tools/ghidra \
    -postScript ExportFunctions.java OUT/ghidra_functions.csv \
    -postScript ImportReport.java OUT/import_report.txt \
    -postScript ExportFunctionAnalysis.java OUT/call_edges.csv OUT/function_metrics.csv
```

- `eboot.elf` is the SELF converted to ELF (target hashes in `target.sha256`).
- Full auto-analysis runs by default. `ExportFunctions.java` refuses a base other than
  `0x400000` and writes address and size only. Its output is for comparison and is not copied to
  `symbols/`: it skips thunks, and its size is the number of bytes Ghidra traced, not the
  function's extent.
- `ExportFunctionAnalysis.java` writes call edges (including unresolved call instructions),
  instruction counts, a conditional-branch complexity estimate, and referenced data addresses.
  Its `analysis_complete` marker means the script scanned that Ghidra-defined function body; it
  does not validate indirect targets or prove function behavior. The script emits no instruction
  bytes or decompiler text. Import the two CSVs into the local scheduler with the matching local
  executable, explicit Ghidra version, and current issue/PR ownership snapshot, for example:

  ```sh
  python3 tools/function_catalog.py --state ~/.cache/paleblood/function-catalog.sqlite3 build \
      --executable /path/to/eboot.elf --edges OUT/call_edges.csv \
      --metrics OUT/function_metrics.csv --analysis-version 'Ghidra 12.0.3 / ExportFunctionAnalysis 1' \
      --claims /path/to/open-work-claims.json
  ```

  The catalog stores the executable hash, not its local path. See
  [Function intelligence catalog](FUNCTION_CATALOG.md) for field definitions and unknown handling.
- After regenerating the function list (below), check it with `tools/validate_functions.py` and
  `tools/function_bounds.py --check`. Once that pull request has merged, `tools/progress_pr.py` regenerates main's progress text
  (CONTRIBUTING.md, "Generated progress"); the pull request itself leaves it alone.
- Full auto-analysis of the eboot took about 26 minutes on the maintainer's machine with the
  heap at 14G. `analyzeHeadless` prints the total at the end of its run.

## The function list

`symbols/ghidra_functions.csv` keeps its name and its two columns, but it is built by
`tools/function_bounds.py` from your own executable, not from Ghidra's function list (issue
#674). Beside it, `symbols/function_kinds.csv` (`address,kind`) says which functions are thunks
and which are static initialisers. Nothing of the executable is written: the two tables hold
addresses, lengths and one of two words.

```
python3 tools/function_bounds.py --elf /path/to/eboot.elf --summary   # the counts, and what differs from the committed list
python3 tools/function_bounds.py --elf /path/to/eboot.elf --write     # regenerate the list and the table of kinds
python3 tools/function_bounds.py --elf /path/to/eboot.elf             # address,size,kind,name of every function
python3 tools/function_bounds.py --check                              # the two committed tables alone (CI, no executable)
```

Everything but `--check` needs the `capstone` Python module; a run takes about ten seconds.

Where a function starts, strongest evidence first:

- an entry of the unwind table (`.eh_frame`, found through the `GNU_EH_FRAME` program header):
  the compiler's own first address and length of every function built with unwind information;
- the ELF header's entry point, and the dynamic table's `DT_INIT` and `DT_FINI`;
- a row of the committed list, unless it lies inside an unwind entry: that is a label inside a
  function, and it is dropped;
- a pointer into code that none of the above covers, from the loader's relocation table
  (`DT_SCE_RELA`). Most of these are the static initialisers of the constructor list (a run of
  relocated code pointers after a word of all ones), which are built without unwind information
  and which nothing calls directly;
- the target of a call, of a jump that leaves its function, or of a `lea`, in code already found.

A function with an unwind entry has that entry's length. Any other is followed from its start,
both ways at every branch, up to the next known start. A call to a function that never returns
ends a path; those callees are recognised from the unwind entries (the call is their last
instruction, several times over), not from a list of addresses.

Every byte is accounted for: from the first function to the end of the last unwind entry, each
byte is in exactly one function or is padding between two (`nop`, `int3`, zero bytes). Anything
else is printed, the tool exits 1, and `--write` refuses. `test/test_function_bounds.py` proves
that on a synthetic executable. `--check` holds the committed list to what follows from it
without the executable: the header and number format, ascending order, no overlap, every start
on a 16-byte boundary, and fewer than 32 bytes between the end of one function and the start of
the next (the largest gap in the list is 21), so a hole that could hold an aligned function and
its padding fails CI.

Every function has a kind: `thunk` (the whole function is one direct jump), `static_init` (the
constructor list points at it) or `function`. The list keeps its two columns, because every other
tool reads it; the 7,927 thunks and 18,444 static initialisers are named in
`symbols/function_kinds.csv`, and a function that table does not name is a `function`. `--check`
holds the table to its format and order and to the list: every address in it is a function of the
list. The name of a function nobody has named yet is its kind, an underscore and the eight hex
digits of its address (`thunk_<address>`, `static_init_<address>`, `function_<address>`). The tool
prints it; it is not stored, since the address and the kind already say it.

What this corrected in the Ghidra export of October 2026 (157,757 rows), for the record:

| | Rows |
|---|---:|
| Thunks, which the export skipped by design | 7,927 |
| Static initialisers that only the constructor list points at | 13,331 |
| Other functions with an unwind entry | 32 |
| Functions whose address is only taken with `lea` | 7 |
| Labels inside a function, listed as functions (dropped) | 16 |
| Kept rows whose length changed (40,353 longer, 125 shorter) | 40,478 |

The lengths changed because the export's size was the number of bytes Ghidra traced, not the
function's extent: it left out what Ghidra did not reach, such as the `nop` after a call that
never returns and data stored inside a function. The list now has 179,038 functions and
44,422,799 bytes, and the same list comes out with no committed list to start from.
