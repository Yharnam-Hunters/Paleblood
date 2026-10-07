# Ghidra setup

How the denominator (`symbols/ghidra_functions.csv`) is produced. Keep the project outside
the repository: it holds the game's code.

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
    -postScript ImportReport.java OUT/import_report.txt
```

- `eboot.elf` is the SELF converted to ELF (target hashes in `target.sha256`).
- Full auto-analysis runs by default. `ExportFunctions.java` refuses a base other than
  `0x400000` and writes address and size only.
- Check the export with `tools/validate_functions.py` after copying it to `symbols/`, then run
  `tools/progress.py --update-readme`.
- Full auto-analysis of the eboot took about 26 minutes on the maintainer's machine with the
  heap at 14G. `analyzeHeadless` prints the total at the end of its run.

## Cross-check of the export

The binary's unwind table (`.eh_frame_hdr`) lists the start of every function that has unwind
information, independent of Ghidra. When the export is regenerated, compare it with that list:

- every unwind entry should be either a function in the export or a thunk in Ghidra (the export
  skips thunks by design);
- export functions without an unwind entry are code built without unwind information (or a
  thunk split off the front of a function) and are counted;
- an unwind entry in a block other than `.text`, or a large share of unwind entries that are
  neither exported nor thunks, means the loader misclassified code: stop and investigate.

The first export (October 2026) reconciled completely: no unwind entry outside `.text`, every
unexported one a thunk. The counts are in the commit message that added it.
