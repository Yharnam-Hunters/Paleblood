<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Loading

How the game reads its files: the file interfaces the executable imports from the PS4 kernel, the C library and the bundled Fios module, and the PlayGo calls for chunk and install state.

**At a glance**

- **Scope:** File access, Fios file and directory handles, PlayGo chunks, and the archive and mount layers above them.
- **Verified:** Seven functions directly above the file imports (kernel files, Fios handles, C streams) are replaced and edge-verified, and since 12 and 13 November the resource loader above them: mount names, the disk file device's open, load requests, the load event queues and transfer tasks (the function table below gives each one's status).
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

### The resource loader above the files

Between the game's request for a resource and the file read sits Dantelion2's resource loader.
Its first layers were replaced on 12 and 13 November, each with an edge-case generator; all of them
are reached in coverage boots to character creation:

- **Mount names.** A path the game opens starts with a mount name and a colon (`name:/...`). The
  PS4 disk file device keeps two tables of (name, target) pairs and rewrites a path's prefix with
  them in rounds, aliases first and then one root, until none matches; the wide path token scanner
  they use was called about 11,000 times in one boot ([#685](https://github.com/Yharnam-Hunters/Paleblood/pull/685)). Its open then picks the device
  registered under the path's mount name, or tries every unnamed device until one opens
  ([#710](https://github.com/Yharnam-Hunters/Paleblood/pull/710)).
- **Load requests and events.** The request's copy constructor and the load event queues: a
  request asks the queue's resource manager for a handle, a load event array grows as a vector does,
  and removing a resource's info cancels the first transfer task loading it, under the queue's
  locks ([#685](https://github.com/Yharnam-Hunters/Paleblood/pull/685), [#710](https://github.com/Yharnam-Hunters/Paleblood/pull/710), [#718](https://github.com/Yharnam-Hunters/Paleblood/pull/718)).
- **Transfer tasks.** The task that moves a file's bytes: its initialization asks the disk file
  device where it finds the path, then the read, close, start and update steps run a small state
  machine. Moving from a final state back to a working one is reported ("Tried to shift into lower
  priority.") and done anyway, as the original does ([#799](https://github.com/Yharnam-Hunters/Paleblood/pull/799)).

How these layers are driven during a real load, and in what order, is still being mapped.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x00464b90` | 215 | `loading_stdio_stream_open` | verified | fopen the tagged path with rb/r+b/wb/w+b/ab/a+b from mode bits at +0x28; rewind; map errno (fetched per check) to 0x1001-0x1004; zero +0x30 |
| `0x00829a90` | 33 | `loading_allocator_free_headed_00829a90` | edge-verified | Frees a block with a 16-byte header before its data: clears the header word, sized free of header and size+0x10 (32-bit); null ignored |
| `0x00836840` | 70 | `loading_tagged_string_release_00836840` | edge-verified | Tagged string word: below 2 left alone; odd frees word-1 through the thread allocator (0x00829a90); then cleared |
| `0x0084d5a0` | 158 | `loading_fios_directory_delete` | edge-verified | FIOS directory object deleting destructor: close the DH at +0x28; free entries and both tagged paths; free 0x38 bytes (or the header's size) to the thread allocator |
| `0x00857cd0` | 176 | `loading_fios_open_file` | edge-verified | sceFiosFHOpenSync with the caller's 16-bit open flags; on success a 0x18-byte file object from the thread allocator (vtable 0x055c1e00) marked open |
| `0x0085cda0` | 174 | `loading_fios_open_file_read` | edge-verified | sceFiosFHOpenSync to read; on success a 0x18-byte file object (vtable 0x055c2190) open when the handle is not negative |
| `0x013e68f0` | 160 | `loading_path_table_find_013e68f0` | verified | FD4 hashed table find by path key: hash (x*0x89, ASCII letters folded, backslash as slash) cached in the key, bucket chain at +0x58, compare 0x02db46f0 |
| `0x013e6ff0` | 91 | `loading_resource_base_construct_013e6ff0` | edge-verified | Resource base: builds the object base with the heap reference, then its vtable, an empty file handle, its state bytes and the heap reference's word |
| `0x013e7090` | 56 | `loading_resource_base_destroy_013e7090` | edge-verified | Resource base: writes its vtable back, lets go of the handle's file, destroys the handle and then the object base |
| `0x0237e640` | 129 | `loading_file_copy_construct_0237e640` | edge-verified | constructor: resource base with the resource heap and no copy (class a; vtable 0x057acc60) |
| `0x0237e6d0` | 162 | `loading_file_copy_delete_0237e6d0` | edge-verified | deleting destructor (class a) |
| `0x0237e780` | 154 | `loading_file_copy_destroy_0237e780` | edge-verified | destructor: free the file copy through DLNew release then the resource base (class a) |
| `0x0237e820` | 342 | `loading_file_copy_copy_file_0237e820` | edge-verified | vtable slot 7: copy the loaded file bytes into a buffer of its own while holding the file (class a) |
| `0x0237f3f0` | 129 | `loading_file_copy_construct_0237f3f0` | edge-verified | constructor: resource base with the resource heap and no copy (class b; vtable 0x057acd50) |
| `0x0237f480` | 162 | `loading_file_copy_delete_0237f480` | edge-verified | deleting destructor (class b) |
| `0x0237f530` | 154 | `loading_file_copy_destroy_0237f530` | edge-verified | destructor: free the file copy through DLNew release then the resource base (class b) |
| `0x0237f5d0` | 342 | `loading_file_copy_copy_file_0237f5d0` | edge-verified | vtable slot 7: copy the loaded file bytes into a buffer of its own while holding the file (class b) |
| `0x023801a0` | 129 | `loading_file_copy_construct_023801a0` | edge-verified | constructor: resource base with the resource heap and no copy (class c; vtable 0x057ace40) |
| `0x02380230` | 162 | `loading_file_copy_delete_02380230` | edge-verified | deleting destructor (class c) |
| `0x023802e0` | 154 | `loading_file_copy_destroy_023802e0` | edge-verified | destructor: free the file copy through DLNew release then the resource base (class c) |
| `0x02380380` | 342 | `loading_file_copy_copy_file_02380380` | edge-verified | vtable slot 7: copy the loaded file bytes into a buffer of its own while holding the file (class c) |
| `0x0246be70` | 377 | `loading_disk_file_device_instance_0246be70` | edge-verified | Ps4DiskFileDevice singleton: under its own light mutex, builds the device in static storage (0x0246bff0) and registers its release at quit (0x024712e0) with the quit query |
| `0x0246cf50` | 549 | `loading_disk_device_resolve_root_0246cf50` | verified | Ps4DiskFileDevice root mounts: split "name:" off the path; first entry with that name replaces name+2 chars with its target; true unless the name is "title" |
| `0x0246d180` | 558 | `loading_disk_device_resolve_alias_0246d180` | verified | Ps4DiskFileDevice alias mounts: same code as 0x0246cf50 on the alias table (+0x80) |
| `0x0246d550` | 530 | `loading_disk_device_open_0246d550` | edge-verified | Ps4DiskFileDevice open: keep the requested path (thread heap); resolve aliases/roots in rounds; open only if an alias matched; through the named device or each unnamed one; under the device lock |
| `0x0246e3d0` | 573 | `loading_disk_file_device_locate_0246e3d0` | edge-verified | Where a path is found: resolves a copy of its mount prefixes (rounds or roots only), asks the named device (slot 0x20) or each unnamed one until not 2; under the device lock |
| `0x0246e630` | 467 | `loading_disk_device_find_named_device_0246e630` | verified | Ps4DiskFileDevice: device registered under the path mount name (token at ":"; wcslen+wcsncmp); under the device lock |
| `0x0246e810` | 390 | `loading_disk_device_open_on_device_0246e810` | edge-verified | Ps4DiskFileDevice: open through one device on a working copy of the path (path copy heap); on success the path takes the copy |
| `0x0246e9a0` | 714 | `loading_disk_device_open_on_unnamed_devices_0246e9a0` | edge-verified | Ps4DiskFileDevice: try each device not registered under a name; in order; on a working copy of the path; under the device lock |
| `0x02470d50` | 517 | `loading_mount_table_push_back_02470d50` | edge-verified | mount table push_back: grow 1.5x when full (source inside the table found again); copy name and target; end moves on even without a buffer |
| `0x024712e0` | 46 | `loading_disk_file_device_release_at_quit_024712e0` | edge-verified | Disk file device release at quit: unless the destroyed byte is set, destroys the instance (0x0246caa0), clears the instance pointer and sets the byte to 1 |
| `0x024716d0` | 539 | `loading_mount_table_reallocate_024716d0` | edge-verified | mount table reallocate to exact capacity: copy entries; destroy old (heap text freed); free old buffer; failed allocation: transaction jump or report |
| `0x02479710` | 156 | `loading_owned_object_release_02479710` | edge-verified | Releases the two parts a transfer task owns at +0x90 (+0x10 then +0x08): slot +0xb0, then destroy and free through its allocator; clears the slot |
| `0x024bcfa0` | 49 | `loading_result_construct_024bcfa0` | edge-verified | Load result base constructor: vtable, four request words, code and word |
| `0x024bcfe0` | 89 | `loading_result_copy_024bcfe0` | edge-verified | Load result base copy constructor: vtable, request words cleared then copied, code and word |
| `0x024bd080` | 50 | `loading_result_assign_024bd080` | edge-verified | Result base's copy assignment: the four request words one by one, the code and the word; the vtable and the handle stay. Returns the entry |
| `0x024bd310` | 65 | `loading_resource_handle_assign_024bd310` | edge-verified | Resource handle assignment: unless the infos match, drops the old info reference, takes the source info and adds a reference; returns the handle |
| `0x024bd470` | 28 | `loading_resource_handle_construct_024bd470` | edge-verified | Base resource handle constructor: stores its vtable and the info, takes a reference (0x02a3a300) when the info is not null |
| `0x024bd4c0` | 25 | `loading_resource_handle_destroy_024bd4c0` | edge-verified | Base resource handle destructor: stores its vtable, releases the info (0x024bd930) when not null |
| `0x024bd930` | 249 | `loading_resource_info_release_024bd930` | edge-verified | DLResourceInfo drop reference: last one sets releasing once (shared lock); manager release; cancel pending requests from the stream queue |
| `0x024be250` | 343 | `loading_load_request_construct_024be250` | edge-verified | resource load request base constructor: clear words; keep the third argument at +0x20; name string on the request heap (heap check) from wcslen; tail via 0x024be8e0 |
| `0x024be3b0` | 558 | `loading_load_request_copy_024be3b0` | edge-verified | resource load request copy constructor: base vtable; words/bytes copied; name on the request heap; strings/array via 0x024be8e0 then assigned (strings skip self-copy) |
| `0x024be630` | 437 | `loading_load_request_assign_024be630` | edge-verified | Load request copy assignment: words, bytes and owns copied; strings assigned unless self-assignment; data array assigned; returns the request |
| `0x024be8e0` | 362 | `loading_load_request_construct_tail_024be8e0` | edge-verified | resource load request tail: two owning empty wide strings and an empty byte array on the default container heap (heap check each) |
| `0x024c1530` | 152 | `loading_resource_manager_release_024c1530` | edge-verified | DLResourceManagerImpl release of an info: initialized assert; without flag 0x40 leave the file queue and set the signal; under the lock mark released (0x80) and hand to 0x024c3260 |
| `0x024c1630` | 298 | `loading_resource_manager_find_info_024c1630` | edge-verified | DLResourceManagerImpl: last to first slot whose words match request words 3,1,2 and whose name matches word 0 by low bytes through _Getptolower; its info, else null |
| `0x024c3260` | 626 | `loading_resource_manager_remove_released_024c3260` | edge-verified | DLResourceManagerImpl removal of a released info (flags 0x40+0x80): under the lock free the slot holding it (name emptied; appended to the free slots) and unload the info (0x024bdb80) |
| `0x024d6730` | 283 | `loading_file_task_initialize_024d6730` | edge-verified | FileTransferTask initialize: copy request (keep heap word if none); invalid -> false; base initialize; queue; flag bit 0; request words |
| `0x024d6850` | 247 | `loading_file_task_reset_024d6850` | edge-verified | FileTransferTask reset: end slot unless ended; clear words; destroy+free chained objects; clear chain/heap word/state |
| `0x024d6950` | 197 | `loading_file_task_update_024d6950` | edge-verified | FileTransferTask update: under its lock run state handlers (member-function table) until a stop state |
| `0x024d6e20` | 275 | `loading_file_task_check_read_024d6e20` | edge-verified | FileTransferTask read step: open errors or cancel abort; count pending size; state 8 or 9 |
| `0x024d7460` | 245 | `loading_file_task_start_024d7460` | edge-verified | FileTransferTask start: manager stop or cancel abort; prepare; chunk limit; begin read; state 6; start slot |
| `0x024d7e50` | 309 | `loading_file_task_end_024d7e50` | edge-verified | FileTransferTask end (once): manager hands over the info under its lock; completed -> info loaded + queue reports it; else info flag cleared |
| `0x024d8210` | 605 | `loading_file_task_prepare_024d8210` | edge-verified | FileTransferTask read buffer: free the old one; small 0x2c buffer inside the task or allocate read size (manager allow/release callbacks) |
| `0x024d8470` | 755 | `loading_file_task_begin_read_024d8470` | edge-verified | FileTransferTask read start: build the 0xb0-byte read request and queue it to the task reader under its lock; close status 3 for a held read object or info read flag bit 0 |
| `0x024dd540` | 487 | `loading_transfer_task_initialize_024dd540` | edge-verified | TransferTask initialize: clear state; keep manager/info/word; heap by disk device location; claim info; ready |
| `0x024dd8a0` | 40 | `loading_transfer_task_cancel_024dd8a0` | edge-verified | Atomically sets bit 0 (and bit 1 with the flag) of the state at +0x160 unless bit 2 is set; returns true |
| `0x024dde50` | 200 | `loading_transfer_task_check_read_024dde50` | edge-verified | TransferTask read step: submitted -> reading or abort; open errors 0x10/0x11/0x12 abort; else fail code 4 |
| `0x024ddf20` | 190 | `loading_transfer_task_check_close_024ddf20` | edge-verified | TransferTask close step: free read object; closed and not cancelled -> request close; else finish once |
| `0x024de230` | 116 | `loading_memory_stream_read_024de230` | edge-verified | VirtualMemoryInputStream read: -1 if not open; min(count; left) cut to 32 bits; copy and advance position and +0x20 |
| `0x02a2aca0` | 180 | `loading_path_token_next_02a2aca0` | verified | wide path token scanner: skip delimiters (wcschr); return length and start of the next token; cursor moves to the delimiter after it |
| `0x02a3a1b0` | 40 | `loading_resource_info_reference_count_02a3a1b0` | edge-verified | Dantelion2 DLResourceInfo reference count: read under the info's own engine mutex (lock forever, result ignored) |
| `0x02a3a2a0` | 45 | `loading_resource_info_drop_reference_02a3a2a0` | edge-verified | Dantelion2 DLResourceInfo drop reference: decrement under the info's own engine mutex; a count at zero stays zero |
| `0x02a3a300` | 36 | `loading_resource_info_add_reference_02a3a300` | edge-verified | Resource info: adds one to the reference count under the info's own mutex (waiting forever) |
| `0x02a692d0` | 19 | `loading_load_queue_report_loaded_02a692d0` | edge-verified | load event queue: report a request through vtable slot 0 with code 1 |
| `0x02a698e0` | 654 | `loading_load_queue_request_02a698e0` | edge-verified | DLResourceLoadEventQueue request (kind 0): validity; manager info or report -0xf8; info remembers the queue; under the lock append an event with a request copy; handle to the info |
| `0x02a69b70` | 340 | `loading_load_event_array_push_back_02a69b70` | edge-verified | resource load event (0x158) push_back: grow 1.5x when full (source inside found again); copy two words; copy-construct the request; queued vtable |
| `0x02a69cd0` | 654 | `loading_load_queue_request_kind_4_02a69cd0` | edge-verified | DLResourceLoadEventQueue request (kind 4): same code as 0x02a698e0 |
| `0x02a69fb0` | 105 | `loading_finished_queue_front_02a69fb0` | edge-verified | Finished-entry list front: under the lock copy the first entry when the count is not 0 |
| `0x02a6a020` | 140 | `loading_finished_queue_pop_02a6a020` | edge-verified | Finished-entry list pop: under the lock unlink the first node; destroy its entry; free; count one less |
| `0x02a6a7f0` | 448 | `loading_load_queue_dispatch_02a6a7f0` | edge-verified | file load event queue: under its lock hand each event to its kind handler (0 3 4); erase all |
| `0x02a6a9e0` | 465 | `loading_load_queue_start_file_task_02a6a9e0` | edge-verified | file load event queue: take a file task slot from the pool (new 32-slot block if none); initialize; append to transfer tasks |
| `0x02a6abc0` | 392 | `loading_load_queue_cancel_event_02a6abc0` | edge-verified | DLResourceFileLoadEventQueue kind 3 event: find the transfer task loading the event's info in six task lists (read-write lock) and cancel it |
| `0x02a6ad50` | 465 | `loading_load_queue_start_file_task_no_buffer_02a6ad50` | edge-verified | file load event queue kind 4 event: start a file task as 0x02a6a9e0 with initialization flag 1 (no read buffer) |
| `0x02a6af30` | 423 | `loading_load_queue_remove_02a6af30` | edge-verified | DLResourceFileLoadEventQueue: under the queue lock find the transfer task loading an info in six task lists (read-write lock) and cancel it |
| `0x02a6b0e0` | 184 | `loading_finished_queue_post_02a6b0e0` | edge-verified | load event queue: under its lock append a finished entry (request words code word + info handle) |
| `0x02a6b340` | 218 | `loading_transfer_task_list_push_back_02a6b340` | edge-verified | Transfer task pointer array push_back: grow to 1.5x when full (0x02a6b420), re-find an inside item, store unless no buffer, end += 8 |
| `0x02a6b520` | 395 | `loading_file_load_slots_construct_02a6b520` | edge-verified | file load event queue: construct its 32 slots (0x338 bytes each) in order with 0x024d63f0 |
| `0x02a6bc50` | 618 | `loading_load_event_array_reallocate_02a6bc50` | edge-verified | load event array reallocate to exact capacity: copy events; destroy old (base vtable; data freed; three strings released); free old buffer |
| `0x02a6c690` | 218 | `loading_request_pointer_array_push_back_02a6c690` | edge-verified | Stream queue request pointer array push_back: grow to 1.5x when full (0x02a6f3d0), re-find an inside item, store unless no buffer, end += 8 |
| `0x02a6cd90` | 635 | `loading_stream_queue_request_02a6cd90` | edge-verified | DLResourceStreamLoadEventQueue request: validity; manager info or report -0xf8; stream handle; under the lock append an event with a request copy and its extra byte; handle copy |
| `0x02a6d010` | 393 | `loading_stream_event_array_push_back_02a6d010` | edge-verified | DLResourceStreamLoadEventQueue event array push_back (0x160 events; same code as 0x02a69b70 plus the byte at +0x158; stream request vtable) |
| `0x02a6d8e0` | 224 | `loading_stream_queue_remove_02a6d8e0` | edge-verified | DLResourceStreamLoadEventQueue remove request: pool range assert; erase from the in-use array (memmove); append to the give-back array |
| `0x02a6f140` | 645 | `loading_stream_event_array_reallocate_02a6f140` | edge-verified | DLResourceStreamLoadEventQueue event array reallocate (same code as 0x02a6bc50 for 0x160 events) |
| `0x02a70890` | 141 | `loading_kernel_file_open` | verified | sceKernelOpen with FreeBSD flags and permissions from the platform mode bits; store the descriptor and set the open bit; -1 on error |
| `0x02a70920` | 46 | `loading_kernel_file_read` | edge-verified | sceKernelRead the file's descriptor; -1 on a negative count; else store the count when asked and return 0 |
| `0x02a70a90` | 84 | `loading_stream_handle_construct_02a70a90` | edge-verified | Stream handle constructor: resource handle; stream vtable; queue; info remembers the queue when both are set |
| `0x02aa4a30` | 128 | `loading_finished_entry_construct_02aa4a30` | edge-verified | finished entry constructor: result base + handle to the info the manager finds for the request words |
| `0x02aa4ab0` | 75 | `loading_finished_entry_copy_construct_02aa4ab0` | edge-verified | finished entry copy constructor: result base copy + handle assign |
| `0x02aa4b40` | 46 | `loading_finished_entry_destroy_02aa4b40` | edge-verified | finished entry destructor: drop the handle then the result base |
| `0x02aa4b90` | 42 | `loading_finished_entry_assign_02aa4b90` | edge-verified | Finished entry assignment: result base assign (0x024bd080) then resource handle assign; returns the entry |
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
