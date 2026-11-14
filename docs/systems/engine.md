<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Engine

The engine's shared plumbing that every system leans on: its fatal error report, its check for missing singletons, its reference-counted objects, and small helpers such as writing a line to the standard error stream.

**At a glance**

- **Scope:** Engine-wide helpers in Paleblood's `game/engine/`: the fatal error report and singleton check, reference counting, and replaced engine utility functions.
- **Verified:** `engine_write_stderr`, the string helpers (comparisons, appends, case changes), wide-string set-up, owner storage, a word-and-product store and a handle-table lookup are edge-verified against constructed cases. Since 12 and 13 November most of the engine's memory system (its heaps and allocators), its threads and its kernel synchronization objects are replaced too; the function table below gives each one's status.
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

### Memory: the engine's heaps

The engine (FromSoftware's Dantelion2 library) never asks the system for memory one object at a
time. It carves a few large ranges into heaps, each with its own strategy, and every system
allocates from one of them. On 12 and 13 November the heaps' allocation, free, set-up and
diagnostic paths were replaced, function by function, each with its own edge-case generator that
builds heaps of known shape:

- **The robust heap** (`DLRobustHeap`): 31 small-size free lists plus a red-black tree of larger
  free blocks. It allocates by best fit from the bottom or by the highest-addressed fit from the
  top, aligned or not, and its free reports a double free or an address outside the heap through
  the engine's error report, then frees the block anyway, as the game does
  ([#667](https://github.com/Yharnam-Hunters/Paleblood/pull/667), [#670](https://github.com/Yharnam-Hunters/Paleblood/pull/670), [#708](https://github.com/Yharnam-Hunters/Paleblood/pull/708), [#721](https://github.com/Yharnam-Hunters/Paleblood/pull/721)).
- **The regular heap** (`DLRegularHeap`): 64 size lists; its free was the most called heap function
  in a coverage boot to character creation ([#675](https://github.com/Yharnam-Hunters/Paleblood/pull/675), [#708](https://github.com/Yharnam-Hunters/Paleblood/pull/708), [#721](https://github.com/Yharnam-Hunters/Paleblood/pull/721)). A locked variant
  guards it with a mutex, and the heap manager keeps every heap's range in an array sorted by start
  under a read-write lock ([#758](https://github.com/Yharnam-Hunters/Paleblood/pull/758), by [@FFwLo](https://github.com/FFwLo)).
- **The segregated heaps** (`DLSegregatedBiHeap`, `DLSegregatedRegularHeap`): the block
  descriptors live outside the memory they manage, handed out by a fixed-size allocator
  (`DLFixedAllocator`); the bi-heap allocates from both ends and checks its own consistency before
  reporting a bad free ([#677](https://github.com/Yharnam-Hunters/Paleblood/pull/677), [#680](https://github.com/Yharnam-Hunters/Paleblood/pull/680), [#690](https://github.com/Yharnam-Hunters/Paleblood/pull/690), [#717](https://github.com/Yharnam-Hunters/Paleblood/pull/717)).
- **Small objects and per-thread memory**: the small object allocators and their fast variants,
  the per-thread temporary heap (well over a hundred functions create it on entry) and the linear
  heap, used from both ends ([#721](https://github.com/Yharnam-Hunters/Paleblood/pull/721), [#803](https://github.com/Yharnam-Hunters/Paleblood/pull/803)).
- **The user interface's heap**: identified as Scaleform 4's page-table heap from its page
  signature, with bins of free blocks and a two-bits-per-unit page bitset; its allocation,
  aligned allocation, reallocation in place and free are replaced ([#699](https://github.com/Yharnam-Hunters/Paleblood/pull/699), [#732](https://github.com/Yharnam-Hunters/Paleblood/pull/732)).

The same work replaced the engine's thread helpers, condition signals and semaphores
([#803](https://github.com/Yharnam-Hunters/Paleblood/pull/803); the engine's mutex wrappers are on the [Kernel](kernel.md) page) and, by
[@FFwLo](https://github.com/FFwLo), the wide string's assign from part of another string and its
growth ([#737](https://github.com/Yharnam-Hunters/Paleblood/pull/737)).

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
| `0x00819040` | 190 | `engine_hk_array_reserve_00819040` | edge-verified | hkArray reserve: count*size bytes (32-bit) reallocated (slot 0x30) or allocated and copied; capacity = answered bytes / size; failure: no buffer, 1 |
| `0x00819100` | 169 | `engine_hk_array_reserve_more_00819100` | edge-verified | hkArray reserve more: twice the size (1 when empty) times the element size, reallocated or allocated and copied, unchecked; capacity = bytes / size |
| `0x0111a740` | 20 | `engine_write_stderr` | edge-verified | Forward the string and imported _Stderr object address to fputs; ignore its return and return zero; synthetic wire edge cases only |
| `0x011c4510` | 15 | `engine_store_word_product` | verified | Store low 32-bit input at owner+0x18 and modulo-32-bit product times four at owner+0x1c; valid unaligned byte view and full zero return; field meanings unknown |
| `0x011c47f0` | 29 | `engine_string_length` | verified | Engine strlen; scan to the first NUL and return the low 32 length bits zero-extended into RAX; other incoming GP registers are ignored |
| `0x011c4870` | 51 | `engine_string_append` | verified | NUL-terminated string append returns original destination; includes prefixes beyond four bytes and finite aliases |
| `0x011c48b0` | 60 | `engine_string_append_counted` | edge-verified | Bounded string append uses low32 count; zero count touches neither pointer; appends NUL after count bytes when needed |
| `0x011c48f0` | 18 | `engine_ascii_to_lower` | edge-verified | ASCII lowercase of the low input byte; sign-extend the result byte to 32 bits; the upper 32 bits of RAX are zero |
| `0x011c4910` | 44 | `engine_ascii_to_upper_in_place` | edge-verified | Convert ASCII a-z to A-Z in place through NUL; retain all other bytes and return original pointer; caller 0x01127ca0 |
| `0x011c4940` | 44 | `engine_ascii_to_lower_in_place` | edge-verified | Convert ASCII A-Z to a-z in place through NUL; retain all other bytes and return original pointer; caller 0x01127ca0 |
| `0x011c4970` | 29 | `engine_compare_signed_strings` | edge-verified | Compare zero-terminated strings by signed bytes and return their first differing byte subtraction |
| `0x011c4990` | 47 | `engine_compare_signed_prefix` | verified | Compare signed bytes up to a signed 32-bit bound; always compare the first byte even for zero or negative lengths |
| `0x011c49c0` | 56 | `engine_compare_ascii_ignoring_case` | edge-verified | Case-insensitive string comparison folds ASCII uppercase bytes then returns signed-byte difference as int32; bounded aliases covered |
| `0x01314320` | 54 | `engine_entry_manager_resolve` | edge-verified | Resolve an enabled low32 handle index with unsigned bounds; reject full-width handle and entry sentinels; return opaque table pointers unchanged |
| `0x0132c590` | 49 | `engine_release_owner_storage` | edge-verified | Free nonnull owner storage through its allocator before clearing the field; wider role unknown; caller 0x02713870 |
| `0x0132c6c0` | 81 | `engine_resize_owner_storage` | edge-verified | Free old owner storage before clearing; reload allocator after callback and allocate requested low32 byte count aligned to 16; caller 0x02713870 |
| `0x01398290` | 127 | `engine_fd4_handle_less_01398290` | edge-verified | FD4 handle ordering: both references valid in the first's table and the same table; versions differ: refresh both (0x013e99b0); then first value < second (signed) |
| `0x013e9040` | 113 | `engine_fd4_handle_is_valid_013e9040` | verified | FD4 handle check: nonzero handle, index <= table's last index (inclusive), entry (0xe0 B) generation byte at +0x2c equals the handle's |
| `0x013e99b0` | 126 | `engine_fd4_handle_refresh_013e99b0` | edge-verified | FD4 handle refresh: when the reference's handle is valid, copies the table's version (+0x60) and the entry's value into the reference |
| `0x013e9ee0` | 8 | `engine_fd4_handle_is_set_013e9ee0` | original | FD4 handle is set: the 8-byte handle is nonzero as a whole; called out of line by the handle check 0x013e9040; too short to hook, matched byte-exact instead |
| `0x0198bf70` | 161 | `engine_map_ins_request_update_0198bf70` | verified | MapInsUpdateHelper: set update request bit 4, queue the instance id once (queued byte bit 0); a full queue is reported and pushed anyway |
| `0x019a7090` | 115 | `engine_obj_ins_request_update_019a7090` | verified | ObjInsUpdateHelper: step part +0x210, set request 0x20, queue the id once (flag 0x1000); full queue: reported, then the flags reloaded, / 0x1000, id pushed |
| `0x01c44210` | 212 | `engine_obj_ins_set_registration_01c44210` | edge-verified | ObjInsUpdateHelper: move a registration to a new item in the owner's pointer array (swap-remove the old, append, grow 0x00819100 when full) |
| `0x0217ffb0` | 205 | `engine_initialize_static_wide_string_0217ffb0` | verified | Guarded static empty wide string; byte-only entry test; captured allocator; exact partial stores; destructor registration before full guard publication |
| `0x023392d0` | 1121 | `engine_scaleform_heap_reallocate_023392d0` | edge-verified | Scaleform MemoryHeapPT realloc: small-page block resized in place under 0x800 (else moved) / large block found in the page manager's radix tree; heap lock then page-manager lock |
| `0x0233a7f0` | 803 | `engine_scaleform_heap_bin_allocate_0233a7f0` | edge-verified | Scaleform small-block allocation: bins' best fit; page from the block or its page header; rest binned; bitset marked |
| `0x0233ab20` | 680 | `engine_scaleform_heap_bin_free_0233ab20` | edge-verified | Scaleform small-block free: length from the bitset; a free neighbour unit tail-jumps to the merge; else to the head of its bin |
| `0x0233ae40` | 1188 | `engine_scaleform_heap_bin_allocate_aligned_0233ae40` | edge-verified | Scaleform aligned small-block allocation: aligned best fit; front and back remainders binned; bitset marked |
| `0x0233b2f0` | 1551 | `engine_scaleform_heap_bin_reallocate_0233b2f0` | edge-verified | Scaleform in-place small-block resize: shrink binning the end (joined with a free next block); grow from a free next block within the page half; bin unlink moves the head to prev |
| `0x0233bbe0` | 313 | `engine_scaleform_heap_add_page_0233bbe0` | edge-verified | Scaleform engine page add: limit handler asked when a page would pass the limit; under the page-manager lock a page gets its table location, goes to the bins and the page ring; retry set |
| `0x0233bd20` | 326 | `engine_scaleform_heap_add_page_locked_0233bd20` | edge-verified | Scaleform engine page add for a caller holding the page-manager lock (released around the limit handler); otherwise as 0x0233bbe0 |
| `0x0233be70` | 548 | `engine_scaleform_heap_release_page_0233be70` | edge-verified | Scaleform heap: releases an empty page (low byte of arg 3: caller holds the page manager lock). Bins, engine ring, headers cleared, memory freed, descriptor to the spare ring |
| `0x0233c0a0` | 167 | `engine_scaleform_heap_free_0233c0a0` | edge-verified | Scaleform engine free: bins free; used bytes down; header counts down (the last decides); page released at zero; count down |
| `0x0233c660` | 528 | `engine_scaleform_heap_allocate_large_0233c660` | edge-verified | Scaleform heap: large block (request rounded to 8, node after it) from the arena allocator; limit handler first; node into the page manager tree; reports and counts it; retry flag |
| `0x0233c870` | 474 | `engine_scaleform_heap_allocate_0233c870` | edge-verified | Scaleform engine allocation: alignment > 16 to the aligned path; <= 0x200 from the bins (rounded to 16) with page-add retries; larger under the page-manager lock unless held |
| `0x0233ca70` | 464 | `engine_scaleform_heap_allocate_aligned_0233ca70` | edge-verified | Scaleform engine aligned allocation: <= 0x200 from the bins (alignment >= 16) with page-add retries; larger (>= 8-aligned and long; rounded to 8) to the large allocation (locked unless held) |
| `0x0233d140` | 347 | `engine_scaleform_heap_reallocate_small_moving_0233d140` | edge-verified | Scaleform small-block move: engine allocation (0 on failure); copies min(old bitset length, reported length); frees the old block as the engine free does |
| `0x0233d750` | 383 | `engine_scaleform_heap_bins_take_best_0233d750` | edge-verified | Scaleform bins best fit: head of the first non-empty bin from the length's own (last bin unchecked); head moves to prev |
| `0x0233d8d0` | 573 | `engine_scaleform_heap_bins_take_best_aligned_0233d8d0` | edge-verified | Scaleform bins aligned best fit: from the first non-empty bin up; each ring walked backwards for a block holding the length at its first aligned address |
| `0x0233dc60` | 739 | `engine_scaleform_heap_bins_merge_free_0233dc60` | edge-verified | Scaleform merge of a freed block with free neighbours (previous through its tail copy); joined block to the head of the joined length's bin |
| `0x0247b720` | 338 | `engine_allocator_of_0247b720` | edge-verified | Allocator owning a pointer: the thread temporary heap if it holds it; else the last registry range holding it (under the read-write lock); else the fallback allocator 0x0247fa50 |
| `0x0247c000` | 1112 | `engine_heap_manager_register_area_0247c000` | edge-verified | DLHeapManager area registration: under the write lock; sorted insert after the last area starting at or below; same heap there refused; overrun reported; vector growth |
| `0x0247c590` | 70 | `engine_locked_regular_heap_construct_0247c590` | edge-verified | Locked regular heap constructor: heap state zeroed (memset); DLLightMutex vtable and cleared flag; allocator interface vtable and owner |
| `0x0247c5e0` | 342 | `engine_locked_regular_heap_initialize_0247c5e0` | edge-verified | Locked regular heap set-up: no memory refused; light mutex initialized (PthreadMutex 0x02483fe0) unless its vtable says so; then the regular heap set-up inlined |
| `0x0247d250` | 304 | `engine_temporary_heap_create_for_thread_0247d250` | edge-verified | DLTemporaryHeap per-thread creation: the thread's size setting (0x20000 when 0; under 0x38 reported); memory from the default allocator; empty bump region after the 0x38-byte object |
| `0x0247d3a0` | 140 | `engine_temporary_heap_destroy_for_thread_0247d3a0` | edge-verified | DLTemporaryHeap per-thread destruction: range cleared, memory freed through the default allocator (quit query and allocator made when missing), slot cleared |
| `0x0247d430` | 34 | `engine_temporary_heap_set_size_0247d430` | edge-verified | DLTemporaryHeap size setting for the thread (0 -> 0x20000) |
| `0x0247d470` | 58 | `engine_temporary_heap_allocator_0247d470` | edge-verified | DLTemporaryHeap allocator interface of the thread's heap (+0x20), creating the heap first |
| `0x0247d4b0` | 86 | `engine_temporary_heap_allocate_0247d4b0` | edge-verified | DLTemporaryHeap bottom allocation on the thread's heap (linear heap step) |
| `0x0247d510` | 121 | `engine_temporary_heap_allocate_aligned_0247d510` | edge-verified | DLTemporaryHeap aligned bottom allocation on the thread's heap (linear heap step) |
| `0x0247d590` | 31 | `engine_temporary_heap_ensure_0247d590` | edge-verified | DLTemporaryHeap: creates the thread's heap when missing |
| `0x0247d5b0` | 58 | `engine_temporary_heap_mark_0247d5b0` | edge-verified | DLTemporaryHeap mark: the thread heap's bottom |
| `0x0247d5f0` | 110 | `engine_temporary_heap_free_until_0247d5f0` | edge-verified | DLTemporaryHeap FreeUntil on the thread's heap: rewinds the bottom; a mark outside the range reported |
| `0x0247dd30` | 31 | `engine_temporary_heap_ensure_0247dd30` | edge-verified | DLTemporaryHeap: creates the thread's heap when missing (identical copy) |
| `0x0247dd60` | 127 | `engine_temporary_heap_allocate_top_0247dd60` | edge-verified | DLTemporaryHeap 8-aligned top allocation on the thread's heap (linear heap step) |
| `0x0247dde0` | 156 | `engine_temporary_heap_allocate_aligned_top_0247dde0` | edge-verified | DLTemporaryHeap aligned top allocation on the thread's heap (linear heap step) |
| `0x0247e1a0` | 161 | `engine_temporary_heap_free_0247e1a0` | edge-verified | DLTemporaryHeap top block free on the thread's heap (linear heap step) |
| `0x0247e400` | 18 | `engine_condition_signal_construct_0247e400` | edge-verified | DLConditionSignal constructor: vtable, flags cleared |
| `0x0247e420` | 201 | `engine_condition_signal_delete_0247e420` | edge-verified | DLConditionSignal deleting destructor: when created, mutex then condition destroyed (busy stops; errors reported); freed |
| `0x0247e4f0` | 198 | `engine_condition_signal_destroy_base_0247e4f0` | edge-verified | DLConditionSignal base destructor: when created, mutex then condition destroyed; created flag cleared unless busy |
| `0x0247e5c0` | 206 | `engine_condition_signal_finalize_0247e5c0` | edge-verified | DLConditionSignal finalize: when created per vtable, destroys mutex and condition; -2 when busy, else 0 and not created |
| `0x0247e690` | 211 | `engine_condition_signal_create_0247e690` | edge-verified | DLConditionSignal create: skipped when created; argument byte -> signalled, waited cleared; mutex failure returned; ENOMEM/EAGAIN -1; EINVAL/EBUSY/other reported, created |
| `0x0247e780` | 366 | `engine_condition_signal_set_0247e780` | edge-verified | DLConditionSignal set/clear under its mutex; setting wakes every waiter (pthread_cond_broadcast) when one has waited; errors reported; 0 |
| `0x0247e8f0` | 329 | `engine_condition_signal_wait_0247e8f0` | edge-verified | DLConditionSignal wait under its mutex unless already set (marks a waiter); returns the condition wait result or 0 |
| `0x0247ea40` | 61 | `engine_condition_signal_poll_0247ea40` | edge-verified | DLConditionSignal poll: reports an uncreated object; 0 when signalled, else -2 |
| `0x0247ea80` | 40 | `engine_condition_signal_check_0247ea80` | edge-verified | DLConditionSignal check: reports an uncreated object; 0 |
| `0x024846f0` | 105 | `engine_fast_small_object_allocator_delete_024846f0` | edge-verified | DLFastSmallObjectAllocator deleting destructor: every class allocator released (0x02a3bc40), class array freed to the backing allocator, operator delete |
| `0x02484760` | 106 | `engine_fast_small_object_allocator_destroy_02484760` | edge-verified | DLFastSmallObjectAllocator destructor: every class allocator released (0x02a3bc40), class array freed and forgotten |
| `0x024847d0` | 309 | `engine_fast_small_object_allocator_delete_locked_024847d0` | edge-verified | DLFastSmallObjectAllocator deleting destructor, locked variant: classes released, array freed, PthreadMutex torn down (errors reported, busy kept), operator delete |
| `0x02484910` | 307 | `engine_fast_small_object_allocator_destroy_locked_02484910` | edge-verified | DLFastSmallObjectAllocator destructor, locked variant: classes released, array freed, PthreadMutex torn down (errors reported, busy kept) |
| `0x02484ae0` | 74 | `engine_fixed_allocator_release_chunks_02484ae0` | edge-verified | DLFixedAllocator chunk release: left subtree, right subtree, then the node's own chunk back to the backing allocator (vtable +0x70); the tree is left as it was |
| `0x02484b30` | 239 | `engine_fixed_allocator_initialize_02484b30` | edge-verified | DLFixedAllocator set-up: reports a non-power-of-two alignment and room for fewer than two elements; element at least 8 rounded to the alignment; empty chunk list and tree |
| `0x02484c20` | 72 | `engine_fixed_allocator_clear_02484c20` | edge-verified | DLFixedAllocator clear: releases every chunk (0x02484ae0) and forgets them (current, last freed, list, tree); counts untouched |
| `0x02484c70` | 361 | `engine_fixed_allocator_allocate_02484c70` | edge-verified | DLFixedAllocator allocation: front element of the current chunk or the first chunk with room; a new chunk from the backing allocator joins the list and the address tree |
| `0x02484de0` | 295 | `engine_fixed_allocator_free_02484de0` | edge-verified | DLFixedAllocator free: chunk from the last-freed hint or the address tree; element to the front of its chunk; emptied chunk released past two chunks of free elements |
| `0x02484f20` | 162 | `engine_fixed_allocator_shrink_02484f20` | edge-verified | DLFixedAllocator shrink: every chunk in the with-room list whose elements are all free leaves the list and tree and goes back to the backing allocator |
| `0x02485100` | 23 | `engine_linear_heap_allocate_02485100` | edge-verified | DLLinearHeap bottom allocation: the next size bytes, null when the bottom would pass the top |
| `0x02485120` | 56 | `engine_linear_heap_allocate_aligned_02485120` | edge-verified | DLLinearHeap aligned bottom allocation: bottom padded to the alignment first; null for a non-power-of-two alignment or no room |
| `0x02485160` | 38 | `engine_linear_heap_check_owned_02485160` | edge-verified | DLLinearHeap ownership check: reports an address outside [lowest, highest) |
| `0x02485190` | 64 | `engine_linear_heap_allocate_top_02485190` | edge-verified | DLLinearHeap top allocation: an 8-aligned block below the top with a 16-byte header (previous top, in use); null when it would pass the bottom |
| `0x024851d0` | 91 | `engine_linear_heap_allocate_aligned_top_024851d0` | edge-verified | DLLinearHeap aligned top allocation: aligned to at least 8; null for a non-power-of-two alignment or no room |
| `0x02485230` | 113 | `engine_linear_heap_free_02485230` | edge-verified | DLLinearHeap free: the lowest top block moves the top up past the blocks no longer in use; another block is only marked; outside addresses reported |
| `0x024852d0` | 65 | `engine_linear_heap_free_until_024852d0` | edge-verified | DLLinearHeap FreeUntil: rewinds the bottom to a mark below it; a mark outside [lowest, highest] reported |
| `0x02485330` | 16 | `engine_linear_heap_contains_02485330` | edge-verified | DLLinearHeap: whether an address is in [lowest, highest) |
| `0x02485340` | 183 | `engine_linear_heap_walk_02485340` | edge-verified | DLLinearHeap walk step: the bottom used region, the free gap, then each top block up to the end |
| `0x02485500` | 256 | `engine_regular_heap_initialize_02485500` | edge-verified | DLRegularHeap set-up: 64 empty lists; one free block from the first 16-aligned payload to the end block; under 0x40 bytes the heap object is zeroed (memset) and false |
| `0x02485730` | 356 | `engine_regular_heap_allocate_02485730` | verified | DLRegularHeap allocation: first block of the lowest non-empty larger list, else first fit in the own list; rest split off into its list |
| `0x024858a0` | 445 | `engine_regular_heap_free_024858a0` | edge-verified | DLRegularHeap free: range and in-use reports (continue); merge free neighbours (16-byte blocks in no list); merged block to the front of list floor(log2(size)) |
| `0x02485a60` | 748 | `engine_regular_heap_allocate_aligned_02485a60` | verified | DLRegularHeap aligned allocation: power of two; up to 16 the plain one; below 0x400 lowest list above the worst case; else first aligned fit; front and rest stay free |
| `0x02485e20` | 459 | `engine_regular_heap_check_block_02485e20` | edge-verified | DLRegularHeap block check: the first broken word around a payload (next words, flags, back pointer, list links and their back links), or null |
| `0x02486080` | 570 | `engine_robust_heap_check_block_02486080` | edge-verified | DLRobustHeap block check: the first broken word around a payload (next words, flags, back pointer, small-list links or large-tree size and links), or null |
| `0x024863f0` | 841 | `engine_robust_heap_initialize_024863f0` | edge-verified | DLRobustHeap set-up: 31 list sentinels at the memory rounded to 16 then 128; one free block to the end rounded down to 16 less a header-only end block; filed by size; false for no or too little memory |
| `0x02486810` | 1192 | `engine_robust_heap_allocate_02486810` | edge-verified | DLRobustHeap best-fit allocation: own small list; larger list split; tree smallest (split in place past 0x200 spare); large: exact or smallest larger |
| `0x02486cd0` | 1693 | `engine_robust_heap_allocate_aligned_02486cd0` | edge-verified | DLRobustHeap aligned allocation from the bottom: first block (own list aligned; larger lists; tree in order) used from its start or with an aligned payload inside |
| `0x02487380` | 675 | `engine_robust_heap_allocate_top_02487380` | edge-verified | DLRobustHeap top allocation: the highest-address free block that fits (lists and tree) is carved from its end; the front stays free |
| `0x02487650` | 1252 | `engine_robust_heap_allocate_aligned_top_02487650` | edge-verified | DLRobustHeap aligned top allocation: highest block whose top aligned payload fits; front and rest stay free |
| `0x02487b50` | 481 | `engine_robust_heap_free_02487b50` | edge-verified | DLRobustHeap free: reports double frees and foreign pointers (then frees anyway); merges with free neighbours and files the result by size |
| `0x02487dd0` | 117 | `engine_segregated_bi_heap_reset_02487dd0` | edge-verified | DLSegregatedBiHeap state reset: clears from the set-up flag to the end of the state (terminator, chains, range, counters) |
| `0x02487e50` | 469 | `engine_segregated_bi_heap_initialize_02487e50` | edge-verified | DLSegregatedBiHeap set-up: 16-aligned range, inline descriptor allocator (0x66 per chunk), terminator at the limit, one free block; no descriptor tears the allocator down and fails |
| `0x02488030` | 677 | `engine_segregated_bi_heap_allocate_bottom_02488030` | edge-verified | DLSegregatedBiHeap bottom allocation: first free block (address order) holding the 16-rounded size aligned; split rest/front via new descriptors; to the bottom chain |
| `0x024882f0` | 815 | `engine_segregated_bi_heap_allocate_top_024882f0` | edge-verified | DLSegregatedBiHeap top allocation: last free block (address order) holding the 16-rounded size aligned; a new descriptor takes its highest aligned place, the gap above stays free; to the top chain |
| `0x02488650` | 203 | `engine_segregated_bi_heap_finalize_02488650` | edge-verified | DLSegregatedBiHeap tear-down: releases the descriptor allocator when set up; clears the terminator, chains and range (counters kept) |
| `0x02488900` | 1000 | `engine_segregated_bi_heap_free_02488900` | edge-verified | DLSegregatedBiHeap free: find in bottom then top chain; merge free neighbours (descriptors back to the pool); relink into the address-ordered free list |
| `0x02488cf0` | 580 | `engine_segregated_bi_heap_is_consistent_02488cf0` | edge-verified | DLSegregatedBiHeap integrity check: passes when not set up; range and terminator sane, no list loops (Floyd), chains in use, address chain and free list linked back and ordered |
| `0x02488f40` | 586 | `engine_segregated_bi_heap_free_top_02488f40` | edge-verified | DLSegregatedBiHeap BackFree: the free with only the top chain searched; own report lines and message |
| `0x02489340` | 482 | `engine_segregated_regular_heap_initialize_02489340` | edge-verified | DLSegregatedRegularHeap set-up: hash table from the allocator (vtable +0x50); descriptor pool (0x66 per 0x1028 chunk); one free block over the 16-aligned range; end descriptor in the heap |
| `0x02489530` | 234 | `engine_segregated_regular_heap_finalize_02489530` | edge-verified | DLSegregatedRegularHeap tear-down: counters, end descriptor, lists and table cleared (count all ones); table freed (vtable +0x70); descriptor pool chunks released and forgotten |
| `0x02489620` | 670 | `engine_segregated_regular_heap_allocate_02489620` | edge-verified | DLSegregatedRegularHeap allocation: first block of the smallest class that surely fits (else first fitting one of its own range class); rest split off |
| `0x024898c0` | 1112 | `engine_segregated_regular_heap_allocate_aligned_024898c0` | edge-verified | DLSegregatedRegularHeap aligned allocation: worst-case class up to 0x400 alignment; else first block holding the size from an aligned address; front and rest stay free |
| `0x02489d20` | 672 | `engine_segregated_regular_heap_allocate_top_02489d20` | edge-verified | DLSegregatedRegularHeap top allocation: highest-address fitting free block; the allocation takes its end; the front stays free |
| `0x02489fc0` | 1081 | `engine_segregated_regular_heap_allocate_aligned_top_02489fc0` | edge-verified | DLSegregatedRegularHeap aligned top allocation: highest-address block holding the size aligned; placed at the highest aligned fit; front and tail stay free |
| `0x0248a400` | 688 | `engine_segregated_regular_heap_free_0248a400` | edge-verified | DLSegregatedRegularHeap free: hash-table lookup (unknown address reported); merges with free neighbours; to the front of its class list |
| `0x0248a9f0` | 159 | `engine_small_object_allocator_delete_0248a9f0` | edge-verified | DLSmallObjectAllocator deleting destructor: every class emptied (chunks released and forgotten), class array freed, operator delete |
| `0x0248aa90` | 160 | `engine_small_object_allocator_destroy_0248aa90` | edge-verified | DLSmallObjectAllocator destructor: every class emptied (chunks released and forgotten), class array freed and forgotten |
| `0x0248ab30` | 363 | `engine_small_object_allocator_delete_locked_0248ab30` | edge-verified | DLSmallObjectAllocator deleting destructor, locked variant: classes emptied, array freed, PthreadMutex torn down (errors reported, busy kept), operator delete |
| `0x0248adc0` | 528 | `engine_thread_construct_0248adc0` | edge-verified | DLThread constructor (default stack): start mutex created and locked, condition signal, name (unnamed when null/empty), thread created; failures reported |
| `0x0248afe0` | 540 | `engine_thread_construct_with_affinity_0248afe0` | edge-verified | DLThread constructor with stack size and core mask: same members; thread created with the mask; failures reported |
| `0x0248b200` | 532 | `engine_thread_construct_sized_0248b200` | edge-verified | DLThread constructor with a stack size: same members; failures reported |
| `0x024df920` | 524 | `engine_debug_print_024df920` | edge-verified | Debug print (format, arguments): lazily builds the print lock; locks it; vprintf when forced or enabled; unlocks |
| `0x024dfb30` | 370 | `engine_debug_print_list_024dfb30` | edge-verified | Debug print with a va_list: lazily builds the print lock (and a light mutex beside it); locks it; vprintf when forced or enabled; unlocks |
| `0x024e2a80` | 253 | `engine_text_multibyte_to_wide_024e2a80` | edge-verified | Bytes to wide string: Shift-JIS tables for kinds 2/3 (lead byte cut off by the count stops), UTF-8 through 0x024e88c0; count stored back, then bytes consumed; characters written |
| `0x024e2b90` | 530 | `engine_text_wide_to_multibyte_024e2b90` | edge-verified | Wide string to bytes: Shift-JIS tables for kinds 2/3 (stops after a terminator, at an unmapped character or a full buffer), UTF-8 for kind 0; count stored back, then characters consumed; bytes written |
| `0x024e2dc0` | 519 | `engine_text_multibyte_size_024e2dc0` | edge-verified | Bytes a wide string needs: UTF-8 1-3, kind 2 one each, Shift-JIS (halfwidth katakana one byte), kind 4 two outside ASCII; terminator counted when reached; other kinds 0 |
| `0x024e2ff0` | 715 | `engine_text_wide_size_024e2ff0` | edge-verified | Wide characters a byte string makes: UTF-8 (malformed or cut-off sequences stop), kind 2 one per byte, Shift-JIS and kind 4 double-byte counts; terminator counted when reached; other kinds 0 |
| `0x024e3390` | 22 | `engine_format_bytes_list_unbounded_024e3390` | edge-verified | DLFormat vsprintf: tail call of the byte format driver 0x024e4240 with room INT_MAX |
| `0x024e33b0` | 166 | `engine_format_bytes_024e33b0` | edge-verified | DLFormat variadic byte-string format (snprintf style): va_start, then the byte format driver 0x024e4240 with out, room, format |
| `0x024e3460` | 127 | `engine_format_allocated_024e3460` | edge-verified | DLFormat into an allocator buffer: measure with 0x024e4240 (null, INT_MAX), allocate length+1 (vtable +0x50), null -> null; empty -> terminator only, else format again with the same va_list |
| `0x024e3e80` | 22 | `engine_format_wide_list_unbounded_024e3e80` | edge-verified | DLFormat vswprintf without a size: tail call of the wide format driver 0x024e59f0 with room INT_MAX |
| `0x024e3ea0` | 166 | `engine_format_wide_024e3ea0` | edge-verified | DLFormat variadic wide format (swprintf style): va_start, then the wide format driver 0x024e59f0 with out, room, format |
| `0x024e3f50` | 184 | `engine_format_wide_unbounded_024e3f50` | edge-verified | DLFormat variadic wide format without a size: va_start, then 0x024e59f0 with room INT_MAX |
| `0x024e4010` | 284 | `engine_format_to_wide_string_variadic_024e4010` | edge-verified | DLFormat variadic into an engine wide string: va_start, then as 0x024e4130 (measure, resize, format again with the same va_list) |
| `0x024e4130` | 128 | `engine_format_to_wide_string_024e4130` | edge-verified | DLFormat into an engine wide string: measure with 0x024e59f0 (null, INT_MAX), resize to the length (fill 0, 0x02a2a650), 0 when empty, else format into its text (inline or heap) with room length+1 |
| `0x024e62b0` | 401 | `engine_format_string_argument_024e62b0` | edge-verified | DLFormat %s argument: narrow string (bytes widened as signed) when the caller's flag 0x40 and an 'h' modifier say so, else wide; null narrow -> <NULL>; written as 0x024e77f0 does |
| `0x024e6c70` | 635 | `engine_format_integer_text_024e6c70` | edge-verified | DLFormat number text, base 2..36 (else 0): signed -> - or + (flag 4); # -> 0x/0X or 0; executable's lower/upper digit tables (flag 0x40); returns the length |
| `0x024e6f00` | 424 | `engine_format_write_integer_024e6f00` | edge-verified | DLFormat number out: fails when room < width or digits; lone 0 at precision 0 dropped unless 0x100; sign/space; pad, # prefix, zeros, digits (count-only keeps cursor), right pad |
| `0x024e70e0` | 430 | `engine_format_integer_024e70e0` | edge-verified | DLFormat %d %i %u %o %x %b (others base 10); 64-bit for l/L/I; # dropped for 0 (%#o keeps it); text via 0x024e6c70, written by 0x024e6f00 |
| `0x024e77f0` | 233 | `engine_format_write_wide_string_024e77f0` | edge-verified | DLFormat wide string out (null -> <NULL>): fails when room < width; cut to room-1 and precision; left pad (zero or space) unless left-justified, right spaces; count-only advances without storing |
| `0x024f8350` | 262 | `engine_tree_insert_fixup_024f8350` | edge-verified | Red-black tree insert fix-up (robust heap, fixed allocator): children cleared, node red; red uncles recoloured upward; black or missing uncle ends with one or two rotations |
| `0x024f8460` | 695 | `engine_tree_remove_024f8460` | edge-verified | Red-black tree remove: the child or the successor takes the place; a black removal is rebalanced (a red sibling rotated above the parent first, then finished in place) |
| `0x027ec640` | 583 | `engine_scaleform_heap_bitset_mark_027ec640` | edge-verified | Scaleform page bitset: mark an allocated block (2-bit codes: 1; 2; 3+len-3; 3 3 + 6-bit len-6; 3 3 3 + 32-bit word); last unit 1 |
| `0x027ec890` | 171 | `engine_scaleform_heap_bitset_units_027ec890` | edge-verified | Scaleform page bitset: read an allocated block's length from its first codes |
| `0x02a279a0` | 429 | `engine_wide_string_assign_from_02a279a0` | edge-verified | Wide string assign(source, position, count): out of range past the end; self-assign cuts the tail, wmemmove front; else grow if capacity < length, wmemcpy |
| `0x02a27b70` | 311 | `engine_wide_string_grow_02a27b70` | edge-verified | Wide string growth: capacity length/7 or old*1.5 (max 0x7ffffffffffffffe); allocate 2*cap+2 align 2, transaction on failure; keep chars, free old heap |
| `0x02a2a310` | 477 | `engine_wide_string_assign_02a2a310` | edge-verified | Wide string assign from counted text: text inside its own characters keeps that part (cut the tail, wmemmove the front); else too long throws, grow 0x02a2a500 if needed, wmemcpy |
| `0x02a2a720` | 477 | `engine_wide_string_assign_counted_02a2a720` | edge-verified | Wide string assign from counted text: text inside the string is the assign from part of itself (inlined); otherwise too long throws, grow if needed, copy |
| `0x02a2d8e0` | 452 | `engine_narrow_string_assign_02a2d8e0` | edge-verified | Narrow string assign from counted text: text inside its own characters keeps that part (cut the tail, memmove the front); else count ~0 throws, grow 0x02a2dac0 if needed, memcpy |
| `0x02a3a840` | 19 | `engine_mutex_construct_02a3a840` | edge-verified | Engine mutex constructor (the print lock base): its vtable 0x056b5b70 and a cleared initialized flag; the handle is left as it was |
| `0x02a3a920` | 49 | `engine_mutex_initialize_02a3a920` | edge-verified | Engine mutex initialize: asks the vtable; else the PthreadMutex initialize (0x02483fe0) on the handle; a nonzero result is returned and the flag stays as it was |
| `0x02a3ae00` | 394 | `engine_semaphore_wait_02a3ae00` | edge-verified | DLSemaphore wait for one unit, forever or up to a timeout; -3 on ETIMEDOUT; ESRCH..EINVAL report chains, unknown errors reported; 0 |
| `0x02a3b0d0` | 122 | `engine_semaphore_signal_02a3b0d0` | edge-verified | DLSemaphore signal: EINVAL ignored; ESRCH and other errors reported; 0 |
| `0x02a3b4d0` | 32 | `engine_thread_create_02a3b4d0` | edge-verified | DLThread: creates the system thread through 0x02a3b310 with every core and 0 in place of the dropped fifth argument |
| `0x02a3b8b0` | 253 | `engine_block_pool_allocate_02a3b8b0` | edge-verified | Block pool: a block from the current page, else the front partial page, else a new page from the allocator (blocks chained); a page left without free blocks goes to the full ring |
| `0x02a3b9b0` | 142 | `engine_block_pool_free_02a3b9b0` | edge-verified | Block pool: a block back on its page's free chain; a full page moves to the partial ring, a page with every block free goes back to the allocator |
| `0x02a3baa0` | 378 | `engine_fast_fixed_allocator_initialize_02a3baa0` | edge-verified | DLFastFixedAllocator set-up: chunk size rounded up to a power of two, chunks aligned to it; blocks rounded to the alignment (at least 8); reports bad sizes and alignment |
| `0x02a3bc40` | 173 | `engine_fast_fixed_allocator_release_02a3bc40` | edge-verified | DLFastFixedAllocator destructor body: every chunk of both chunk lists, then the spare chunk, freed to the backing allocator |
| `0x02a44140` | 59 | `engine_wide_string_destroy` | edge-verified | WideString destructor; unsigned capacity >=8 frees even null; ignores owns; callback before capacity and size reset and two-byte terminator |
| `0x02b4d100` | 477 | `engine_byte_array_assign_02b4d100` | edge-verified | Engine byte array copy assign: self no-op; slot 0x10 queries; empty source empties; memmove over the size or within capacity (rest constructed); else free, allocate exact (align 1), copy |
| `0x02bc0e00` | 196 | `engine_wide_string_construct_02bc0e00` | edge-verified | Wide string from null-terminated text: default allocator (created on first use), container heap check, empty inline string, counted assign 0x02a2a720 with wcslen (0 for empty) |
| `0x02fa1640` | 517 | `engine_fast_small_object_allocator_construct_02fa1640` | edge-verified | DLFastSmallObjectAllocator constructor: size classes from the largest (rounded to the alignment) down by the step, one DLFastFixedAllocator each; bad sizes reported |
| `0x02fa1950` | 552 | `engine_fast_small_object_allocator_construct_locked_02fa1950` | edge-verified | DLFastSmallObjectAllocator constructor, locked variant: creates its mutex, then the same size classes |
| `0x02fa1e20` | 872 | `engine_small_object_allocator_construct_02fa1e20` | edge-verified | DLSmallObjectAllocator constructor: size classes from smallest to largest by the granularity, one checked DLFixedAllocator per class in an array from the backing allocator |
| `0x02fa22a0` | 904 | `engine_small_object_allocator_construct_locked_02fa22a0` | edge-verified | DLSmallObjectAllocator constructor, locked variant: creates its recursive mutex, then the same size classes |
