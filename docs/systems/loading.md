<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Loading

## Summary

The executable's file interface is split across imports from `libkernel`, `libc`, and its bundled
`libSceFios2` module. The import table also names `libScePlayGo` calls for chunk and install
state. The bundled Fios module is mapped as guest code; its exports provide imports used by the
executable and bundled C library.

These rows describe link dependencies, not a trace of calls made during a load. The latest boot
evidence stops at `scePthreadAttrGetaffinity`, before the runtime's first file milestone. Asset,
archive, map, and load-screen behavior therefore remains unobserved on Paleblood's runtime.

## Key functions

No functions tracked yet.
The import table records interfaces the executable and its bundled modules can call:

- `libSceFios2` imports from the executable: `sceFiosDHOpen`, `sceFiosDHOpenSync`,
  `sceFiosDHReadSync`, `sceFiosDHCloseSync`, `sceFiosDirectoryCreate`, `sceFiosFHOpenSync`,
  `sceFiosFHCloseSync`, `sceFiosFHReadSync`, `sceFiosFHWriteSync`, `sceFiosFHSeek`,
  `sceFiosFHTell`, `sceFiosFHSyncSync`, `sceFiosStatSync`, `sceFiosFileDeleteSync`,
  `sceFiosOpDelete`, `sceFiosOpGetActualCount`, and `sceFiosOpWait`. The bundled C library also
  imports `sceFiosDeleteSync`, `sceFiosFHOpenWithModeSync`, `sceFiosIsValidHandle`,
  `sceFiosRenameSync`, `sceFiosFHReadSync`, `sceFiosFHSeek`, `sceFiosFHCloseSync`, and
  `sceFiosFHWriteSync`.
- File-facing `libkernel` imports include `sceKernelOpen`, `sceKernelRead`, `sceKernelLseek`,
  `sceKernelClose`, and `sceKernelFstat`.
- File-facing `libc` imports include `fopen`, `fread`, `fseek`, `ftell`, `rewind`, `fwrite`,
  and `fclose`.
- `libScePlayGo`: `scePlayGoInitialize`, `scePlayGoOpen`, `scePlayGoGetChunkId`,
  `scePlayGoGetLocus`, and `scePlayGoSetInstallSpeed`. No module or runtime provider is recorded
  for these imports in the current source.

The exact imported names and providers are in `symbols/imports.csv`; the list alone does not show
which calls run, their arguments, or their results.

## Structs and globals

No file-handle, Fios operation, archive-index, or PlayGo structure layout has been verified for
the target. Record a structure only after its size and accessed fields are supported by source
analysis or a matching observation. No system-wide global is currently tied to this path.

## Patch points

No loading-specific patch point has been established. A patch should be documented here only
after its address is mapped to a loading function and its effect is checked against the stock
path.

## Open questions

- Which file imports are reached during startup and the order in which they run; the current boot
  stops before the file milestone.
- How Fios file and directory handles are opened, used, and closed, including the relationship
  between synchronous calls and `sceFiosOp*` operations.
- How PlayGo chunk identifiers and install locations affect asset or map streaming.
- How the game's archive and mount layers translate paths into file operations. Document access
  behavior only; archive contents and extracted files are out of scope.
- Which target-specific error and cancellation paths callers rely on.
