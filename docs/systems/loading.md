<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Loading

How the game reads its files: the file interfaces the executable imports from the PS4 kernel, the C library and the bundled Fios module, and the PlayGo calls for chunk and install state.

**At a glance**

- **Scope:** File access, Fios file and directory handles, PlayGo chunks, and the archive and mount layers above them.
- **Verified:** Seven functions directly above the file imports (kernel files, Fios handles, C streams) are replaced and edge-verified.
- **Open:** Which calls run during a load, and in what order. The boot now reads files from your copy, its scratch folder and its saves, but the game's loading code itself is not mapped.

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

### What a boot on Paleblood's runtime reads

The boot recorded on merged `main` passes the file milestone and runs until its time limit
([#408](https://github.com/Yharnam-Hunters/Paleblood/pull/408), [#443](https://github.com/Yharnam-Hunters/Paleblood/pull/443)). What the runtime serves, as the
runtime system-call notes describe:

- **`/app0`**, your own copy of the game, read-only. The first file the game looks for and reads is
  `sprj_mixer.fev`, its sound mixer description. Its movies are read from there too
  ([#419](https://github.com/Yharnam-Hunters/Paleblood/pull/419)).
- **`/data`**, a scratch folder: the engine maps its `temp:` and `title:` drives there and creates a
  fresh `cache.bin` at start-up.
- **`/savedataN`**, a mounted save, kept in a host folder you choose ([#402](https://github.com/Yharnam-Hunters/Paleblood/pull/402)).
- **PlayGo** reports every chunk installed ([#370](https://github.com/Yharnam-Hunters/Paleblood/pull/370)).

This is the runtime's side. Apart from the seven small functions below, the game's own archive,
mount and load-screen code above these calls has not been mapped, so which assets it loads, and in what order, is not documented here yet.

### The first loading functions

Seven small functions on the file path are replaced and edge-verified (Paleblood
[#517](https://github.com/Yharnam-Hunters/Paleblood/pull/517)). They sit directly on the
imports above, one per interface:

**Kernel files.** The loader's platform layer keeps a file as a descriptor and an "open" bit.
`loading_kernel_file_open` (`0x02a70890`) turns the caller's mode bits into FreeBSD open flags:
read and write pick read-only, write-only or read-write; then the first of create-exclusive,
create-and-truncate and create that is set adds `O_CREAT` with `O_EXCL` or `O_TRUNC` (a lone
truncate bit adds `O_TRUNC`). A new file gets permissions 0777 when it is writable and 0555 when
it is opened only to read. This is the call that creates the start-up `temp:/cache.bin`.
`loading_kernel_file_read` (`0x02a70920`) reads through the descriptor and hands back the count.
Both answer 0 or -1; the kernel's own error code is dropped.

**Fios handles.** `loading_fios_open_file` (`0x00857cd0`) and `loading_fios_open_file_read`
(`0x0085cda0`) open a file with `sceFiosFHOpenSync` and wrap the handle in a small engine object
allocated from the calling thread's allocator. `loading_fios_directory_delete` (`0x0084d5a0`) is
the directory object's deleting destructor: it closes the directory handle, frees the listing
and two path strings, and returns the object to the thread allocator.

**C streams.** `loading_stdio_stream_open` (`0x00464b90`) opens a stream with `fopen` in one of
six binary modes (`rb`, `r+b`, `wb`, `w+b`, `ab`, `a+b`) picked by mode bits, rewinds it, and
turns a failure's `errno` into the engine's own codes: not found `0x1001`, denied (`EACCES` or
`EPERM`) `0x1002`, no space `0x1004`, anything else `0x1003`. `loading_stdio_close_files`
(`0x02e67d20`) closes three streams together, last first.

<details>
<summary>How these were verified</summary>

Each function's edge cases pass against the original (72, 19, 15, 14, 11, 30 and 9 cases),
with deliberately wrong versions failing, and an independent review approved all seven. No
community patch edits them. None has been recorded in a game run yet, so they are
edge-verified, not verified.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x00464b90` | 215 | `loading_stdio_stream_open` | edge-verified | fopen the tagged path with rb/r+b/wb/w+b/ab/a+b from mode bits at +0x28; rewind; map errno (fetched per check) to 0x1001-0x1004; zero +0x30 |
| `0x0084d5a0` | 158 | `loading_fios_directory_delete` | edge-verified | FIOS directory object deleting destructor: close the DH at +0x28, free entries and both tagged paths, free 0x38 bytes (or the header's size) to the thread allocator |
| `0x00857cd0` | 176 | `loading_fios_open_file` | edge-verified | sceFiosFHOpenSync with the caller's 16-bit open flags; on success a 0x18-byte file object from the thread allocator (vtable 0x055c1e00) marked open |
| `0x0085cda0` | 174 | `loading_fios_open_file_read` | edge-verified | sceFiosFHOpenSync to read; on success a 0x18-byte file object (vtable 0x055c2190) open when the handle is not negative |
| `0x02a70890` | 141 | `loading_kernel_file_open` | edge-verified | sceKernelOpen with FreeBSD flags and permissions from the platform mode bits; store the descriptor and set the open bit; -1 on error |
| `0x02a70920` | 46 | `loading_kernel_file_read` | edge-verified | sceKernelRead the file's descriptor; -1 on a negative count, else store the count when asked and return 0 |
| `0x02e67d20` | 80 | `loading_stdio_close_files` | edge-verified | fclose and clear the FILE pointers at +0x10, +0x8 and +0x0 in that order, skipping null ones |

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
  `scePlayGoGetLocus`, and `scePlayGoSetInstallSpeed`. The runtime stands in for the
  `libScePlayGo` system module, so loading it succeeds (Paleblood #310). Since Paleblood
  #370 and #405 it answers that every chunk of the package is installed (the chunk count
  comes from your copy's `sce_sys/playgo-manifest.xml`) and accepts the install speed the
  game sets (everything is installed, so there is nothing to pace).

</details>

## Structs and globals

What the seven replaced functions show (the field names are ours):

| Object | Layout |
|---|---|
| Kernel file | descriptor (`+0x0`, 32-bit), flags (`+0x4`, bit 0: open) |
| Fios file object (0x18 bytes) | vtable; header (`+0x8`: a count that starts at 1, and an allocation size, `0xffff` meaning the class's own); Fios handle (`+0xc`); open byte (`+0x10`) |
| Fios directory object (0x38 bytes) | vtable; header (`+0x8`); two tagged path strings (`+0x18`, `+0x20`); directory handle (`+0x28`, 0 when closed); listing (`+0x30`) |
| C stream | tagged path (`+0x10`; the characters start at offset 0xc of the string); open byte (`+0x18`); `FILE` (`+0x20`); mode bits (`+0x28`); error (`+0x2c`); position (`+0x30`) |

The Fios objects come from the calling thread's allocator, reached through a thread-local holder.
No Fios operation, archive-index or PlayGo layout has been verified.

A structure is recorded here only after its size and accessed fields are supported by source
analysis or a matching observation.

## Patch points

No loading-specific patch point has been established. A patch is documented here only after its
address is mapped to a loading function and its effect is checked against the stock path.

## Open questions

- Which file imports are reached during startup and the order in which they run. The boot now gets
  that far, but no trace of the file calls has been recorded yet.
- How Fios file handles are read and closed, and how directories are listed, including the
  relationship between synchronous calls and `sceFiosOp*` operations (opening and the directory
  destructor are mapped).
- What the stream object with the six `fopen` modes and the holder of three `FILE` pointers are
  used for.
- How PlayGo chunk identifiers and install locations affect asset or map streaming.
- How the game's archive and mount layers translate paths into file operations. Document access
  behavior only; archive contents and extracted files are out of scope.
- Which target-specific error and cancellation paths callers rely on.
