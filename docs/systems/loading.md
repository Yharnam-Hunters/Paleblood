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
| `0x00464b90` | 215 | `loading_stdio_stream_open` | verified | fopen the tagged path with rb/r+b/wb/w+b/ab/a+b from mode bits at +0x28; rewind; map errno (fetched per check) to 0x1001-0x1004; zero +0x30 |
| `0x0084d5a0` | 158 | `loading_fios_directory_delete` | edge-verified | FIOS directory object deleting destructor: close the DH at +0x28; free entries and both tagged paths; free 0x38 bytes (or the header's size) to the thread allocator |
| `0x00857cd0` | 176 | `loading_fios_open_file` | edge-verified | sceFiosFHOpenSync with the caller's 16-bit open flags; on success a 0x18-byte file object from the thread allocator (vtable 0x055c1e00) marked open |
| `0x0085cda0` | 174 | `loading_fios_open_file_read` | edge-verified | sceFiosFHOpenSync to read; on success a 0x18-byte file object (vtable 0x055c2190) open when the handle is not negative |
| `0x0246cf50` | 549 | `loading_disk_device_resolve_root_0246cf50` | edge-verified | Ps4DiskFileDevice root mounts: split "name:" off the path; first entry with that name replaces name+2 chars with its target; true unless the name is "title" |
| `0x0246d180` | 558 | `loading_disk_device_resolve_alias_0246d180` | edge-verified | Ps4DiskFileDevice alias mounts: same code as 0x0246cf50 on the alias table (+0x80) |
| `0x0246d550` | 530 | `loading_disk_device_open_0246d550` | edge-verified | Ps4DiskFileDevice open: keep the requested path (thread heap); resolve aliases/roots in rounds; open only if an alias matched; through the named device or each unnamed one; under the device lock |
| `0x0246e630` | 467 | `loading_disk_device_find_named_device_0246e630` | edge-verified | Ps4DiskFileDevice: device registered under the path mount name (token at ":"; wcslen+wcsncmp); under the device lock |
| `0x0246e810` | 390 | `loading_disk_device_open_on_device_0246e810` | edge-verified | Ps4DiskFileDevice: open through one device on a working copy of the path (path copy heap); on success the path takes the copy |
| `0x0246e9a0` | 714 | `loading_disk_device_open_on_unnamed_devices_0246e9a0` | edge-verified | Ps4DiskFileDevice: try each device not registered under a name; in order; on a working copy of the path; under the device lock |
| `0x02470d50` | 517 | `loading_mount_table_push_back_02470d50` | edge-verified | mount table push_back: grow 1.5x when full (source inside the table found again); copy name and target; end moves on even without a buffer |
| `0x024716d0` | 539 | `loading_mount_table_reallocate_024716d0` | edge-verified | mount table reallocate to exact capacity: copy entries; destroy old (heap text freed); free old buffer; failed allocation: transaction jump or report |
| `0x024bd930` | 249 | `loading_resource_info_release_024bd930` | edge-verified | DLResourceInfo drop reference: last one sets releasing once (shared lock); manager release; cancel pending requests from the stream queue |
| `0x024be250` | 343 | `loading_load_request_construct_024be250` | edge-verified | resource load request base constructor: clear words; keep the third argument at +0x20; name string on the request heap (heap check) from wcslen; tail via 0x024be8e0 |
| `0x024be3b0` | 558 | `loading_load_request_copy_024be3b0` | edge-verified | resource load request copy constructor: base vtable; words/bytes copied; name on the request heap; strings/array via 0x024be8e0 then assigned (strings skip self-copy) |
| `0x024be8e0` | 362 | `loading_load_request_construct_tail_024be8e0` | edge-verified | resource load request tail: two owning empty wide strings and an empty byte array on the default container heap (heap check each) |
| `0x024c1530` | 152 | `loading_resource_manager_release_024c1530` | edge-verified | DLResourceManagerImpl release of an info: initialized assert; without flag 0x40 leave the file queue and set the signal; under the lock mark released (0x80) and hand to 0x024c3260 |
| `0x024d6730` | 283 | `loading_file_task_initialize_024d6730` | edge-verified | FileTransferTask initialize: copy request (keep heap word if none); invalid -> false; base initialize; queue; flag bit 0; request words |
| `0x024d6950` | 197 | `loading_file_task_update_024d6950` | edge-verified | FileTransferTask update: under its lock run state handlers (member-function table) until a stop state |
| `0x024d6e20` | 275 | `loading_file_task_check_read_024d6e20` | edge-verified | FileTransferTask read step: open errors or cancel abort; count pending size; state 8 or 9 |
| `0x024d7460` | 245 | `loading_file_task_start_024d7460` | edge-verified | FileTransferTask start: manager stop or cancel abort; prepare; chunk limit; begin read; state 6; start slot |
| `0x024dd540` | 487 | `loading_transfer_task_initialize_024dd540` | edge-verified | TransferTask initialize: clear state; keep manager/info/word; heap by disk device location; claim info; ready |
| `0x024dde50` | 200 | `loading_transfer_task_check_read_024dde50` | edge-verified | TransferTask read step: submitted -> reading or abort; open errors 0x10/0x11/0x12 abort; else fail code 4 |
| `0x024ddf20` | 190 | `loading_transfer_task_check_close_024ddf20` | edge-verified | TransferTask close step: free read object; closed and not cancelled -> request close; else finish once |
| `0x024de230` | 116 | `loading_memory_stream_read_024de230` | edge-verified | VirtualMemoryInputStream read: -1 if not open; min(count; left) cut to 32 bits; copy and advance position and +0x20 |
| `0x02a2aca0` | 180 | `loading_path_token_next_02a2aca0` | edge-verified | wide path token scanner: skip delimiters (wcschr); return length and start of the next token; cursor moves to the delimiter after it |
| `0x02a698e0` | 654 | `loading_load_queue_request_02a698e0` | edge-verified | DLResourceLoadEventQueue request (kind 0): validity; manager info or report -0xf8; info remembers the queue; under the lock append an event with a request copy; handle to the info |
| `0x02a69b70` | 340 | `loading_load_event_array_push_back_02a69b70` | edge-verified | resource load event (0x158) push_back: grow 1.5x when full (source inside found again); copy two words; copy-construct the request; queued vtable |
| `0x02a69cd0` | 654 | `loading_load_queue_request_kind_4_02a69cd0` | edge-verified | DLResourceLoadEventQueue request (kind 4): same code as 0x02a698e0 |
| `0x02a69fb0` | 105 | `loading_finished_queue_front_02a69fb0` | edge-verified | Finished-entry list front: under the lock copy the first entry when the count is not 0 |
| `0x02a6a020` | 140 | `loading_finished_queue_pop_02a6a020` | edge-verified | Finished-entry list pop: under the lock unlink the first node; destroy its entry; free; count one less |
| `0x02a6af30` | 423 | `loading_load_queue_remove_02a6af30` | edge-verified | DLResourceFileLoadEventQueue: under the queue lock find the transfer task loading an info in six task lists (read-write lock) and cancel it |
| `0x02a6b520` | 395 | `loading_file_load_slots_construct_02a6b520` | edge-verified | file load event queue: construct its 32 slots (0x338 bytes each) in order with 0x024d63f0 |
| `0x02a6bc50` | 618 | `loading_load_event_array_reallocate_02a6bc50` | edge-verified | load event array reallocate to exact capacity: copy events; destroy old (base vtable; data freed; three strings released); free old buffer |
| `0x02a6cd90` | 635 | `loading_stream_queue_request_02a6cd90` | edge-verified | DLResourceStreamLoadEventQueue request: validity; manager info or report -0xf8; stream handle; under the lock append an event with a request copy and its extra byte; handle copy |
| `0x02a6d010` | 393 | `loading_stream_event_array_push_back_02a6d010` | edge-verified | DLResourceStreamLoadEventQueue event array push_back (0x160 events; same code as 0x02a69b70 plus the byte at +0x158; stream request vtable) |
| `0x02a6d8e0` | 224 | `loading_stream_queue_remove_02a6d8e0` | edge-verified | DLResourceStreamLoadEventQueue remove request: pool range assert; erase from the in-use array (memmove); append to the give-back array |
| `0x02a6f140` | 645 | `loading_stream_event_array_reallocate_02a6f140` | edge-verified | DLResourceStreamLoadEventQueue event array reallocate (same code as 0x02a6bc50 for 0x160 events) |
| `0x02a70890` | 141 | `loading_kernel_file_open` | verified | sceKernelOpen with FreeBSD flags and permissions from the platform mode bits; store the descriptor and set the open bit; -1 on error |
| `0x02a70920` | 46 | `loading_kernel_file_read` | edge-verified | sceKernelRead the file's descriptor; -1 on a negative count; else store the count when asked and return 0 |
| `0x02a70a90` | 84 | `loading_stream_handle_construct_02a70a90` | edge-verified | Stream handle constructor: resource handle; stream vtable; queue; info remembers the queue when both are set |
| `0x02e67d20` | 80 | `loading_stdio_close_files` | edge-verified | fclose and clear the FILE pointers at +0x10; +0x8 and +0x0 in that order; skipping null ones |

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
