<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Loading

How the game reads its files: the file interfaces the executable imports from the PS4 kernel, the C library and the bundled Fios module, and the PlayGo calls for chunk and install state.

**At a glance**

- **Scope:** File access, Fios file and directory handles, PlayGo chunks, and the archive and mount layers above them.
- **Verified:** Only the link dependencies in the import table; no loading function is mapped yet.
- **Open:** Which calls run during a load, and in what order: the current boot stops before the first file milestone.

## Summary

### Four interfaces to the file system

The executable's file interface is split across imports from four places:

| Provider | What it offers | Example imports |
|---|---|---|
| `libSceFios2` (bundled) | File and directory handles, plus `sceFiosOp*` operations | `sceFiosFHOpenSync`, `sceFiosFHReadSync`, `sceFiosOpWait` |
| `libkernel` | Low-level file calls | `sceKernelOpen`, `sceKernelRead`, `sceKernelClose` |
| `libc` | Standard C file streams | `fopen`, `fread`, `fclose` |
| `libScePlayGo` | Chunk and install state | `scePlayGoGetChunkId`, `scePlayGoGetLocus` |

The bundled Fios module ships with the game and is mapped as guest code. Its exports provide imports used by both the executable and the bundled C
library.

### What the import table can and cannot tell you

These rows describe link dependencies, not a trace of calls made during a load. The import list
shows what the game *can* call; it does not show which calls run, their arguments, or their
results.

The latest boot evidence stops at `scePthreadAttrGetaffinity`, before the runtime's first file
milestone. Asset, archive, map, and load-screen behavior therefore remains unobserved on
Paleblood's runtime.

## Key functions

No functions tracked yet.

The import table records interfaces the executable and its bundled modules can call. The exact
imported names and providers are in `symbols/imports.csv`.

<details>
<summary>Full list of file-facing imports</summary>

- `libSceFios2` imports from the executable: `sceFiosDHOpen`, `sceFiosDHOpenSync`,
  `sceFiosDHReadSync`, `sceFiosDHCloseSync`, `sceFiosDirectoryCreate`, `sceFiosFHOpenSync`,
  `sceFiosFHCloseSync`, `sceFiosFHReadSync`, `sceFiosFHWriteSync`, `sceFiosFHSeek`,
  `sceFiosFHTell`, `sceFiosFHSyncSync`, `sceFiosStatSync`, `sceFiosFileDeleteSync`,
  `sceFiosOpDelete`, `sceFiosOpGetActualCount`, and `sceFiosOpWait`. The bundled C library
  also imports `sceFiosDeleteSync`, `sceFiosFHOpenWithModeSync`, `sceFiosIsValidHandle`,
  `sceFiosRenameSync`, `sceFiosFHReadSync`, `sceFiosFHSeek`, `sceFiosFHCloseSync`, and
  `sceFiosFHWriteSync`.
- File-facing `libkernel` imports include `sceKernelOpen`, `sceKernelRead`,
  `sceKernelLseek`, `sceKernelClose`, and `sceKernelFstat`.
- File-facing `libc` imports include `fopen`, `fread`, `fseek`, `ftell`, `rewind`, `fwrite`,
  and `fclose`.
- `libScePlayGo`: `scePlayGoInitialize`, `scePlayGoOpen`, `scePlayGoGetChunkId`,
  `scePlayGoGetLocus`, and `scePlayGoSetInstallSpeed`. No module or runtime provider is
  recorded for these imports in the current source.

</details>

## Structs and globals

No file-handle, Fios operation, archive-index, or PlayGo structure layout has been verified for
the target. No system-wide global is currently tied to this path.

A structure is recorded here only after its size and accessed fields are supported by source
analysis or a matching observation.

## Patch points

No loading-specific patch point has been established. A patch is documented here only after its
address is mapped to a loading function and its effect is checked against the stock path.

## Open questions

- Which file imports are reached during startup and the order in which they run; the current boot
  stops before the file milestone.
- How Fios file and directory handles are opened, used, and closed, including the relationship
  between synchronous calls and `sceFiosOp*` operations.
- How PlayGo chunk identifiers and install locations affect asset or map streaming.
- How the game's archive and mount layers translate paths into file operations. Document access
  behavior only; archive contents and extracted files are out of scope.
- Which target-specific error and cancellation paths callers rely on.
