<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Menu

The game's menus and dialogs: so far the text input dialog behind the system keyboard and the Dantelion calls under it, the item dialogs' cursor, direction keys and lists, and the game's own glue code around the Scaleform user interface library.

**At a glance**

- **Scope:** Paleblood's `game/menu/`: the text input dialog and Dantelion's system text dialog object, the item selection and item status dialogs, the Scaleform glue (heap adapter, movie screen, texture repository, file set loader, named resources), and two menu families.
- **Verified:** Six item dialog functions are `verified`: the cursor move, the list row, both direction-key reads, the text pair copy and the status label `push_back`. The rest are edge-verified against constructed cases only.
- **Open:** The menus themselves (the Scaleform screens and the code that drives each menu) are mostly not mapped yet.

## Summary

### The text input dialog

Whenever the player types text, the hunter's name at character creation for example, the game
opens the system keyboard dialog through one small object. It holds Dantelion's dialog object, the
game's own state for it (idle, open, finished), how it ended (canceled or entered) and the text the
dialog returned. Four of its functions are replaced and edge-verified
([#727](https://github.com/Yharnam-Hunters/Paleblood/pull/727)):

- **Open** (`0x024162a0`) closes any earlier dialog first; if the system dialog is not already
  open, it passes the settings to Dantelion, opens the dialog and marks it open.
- **Poll** (`0x02416300`) runs once a frame while the dialog is open. When the dialog has
  finished, it copies the text and swaps the characters the game's message markup uses for private
  glyphs (`#`, `%`, `<`, `>`, `?`, `\` and `¥`), so typed text can never be read as markup. It then
  records the state as finished, with the result canceled or entered, and returns true while the
  dialog is still running.
- **Close** (`0x02416040`) waits for an open dialog to end, then empties the text and resets the
  state and result. Like the original, it runs the whole poll first, so a finished dialog's text is
  still copied and escaped before it is emptied.
- **Destructor** (`0x02415fe0`) closes the dialog, frees a heap text buffer through the string's
  allocator and destroys Dantelion's dialog object.

Ghidra's class guess for two of these functions named them parameter code; their callers are menu
code (the message repository and Scaleform), the debug menu and network code, so they form this
new system. In coverage boots the poll ran thousands of times and close a few times; open and the
destructor did not run.

Since [#979](https://github.com/Yharnam-Hunters/Paleblood/pull/979), by
[@FFwLo](https://github.com/FFwLo), the four calls the dialog makes on Dantelion's dialog object are
replaced too. Each goes through the dialog backend singleton (made on first use) and the object's
open flag: **set up** copies the settings unless the system dialog is already open, **open** asks
the backend to open it, **status** reports the backend's status while it is open (-1 otherwise),
and **take text** closes the object and fetches the text once the dialog has accepted or been
canceled.

### The item dialogs

Two item dialogs copy their list entries, each two wide strings with a word before each:
`0x01edf560` copies an entry of the item selection dialog (`GaitemSelectDialog`) into a slot of a
fixed pool, and `0x01f1bca0` copies entry *n* of the item status dialog (`GaitemStatusDialog`)
([#823](https://github.com/Yharnam-Hunters/Paleblood/pull/823)). Both are edge-verified.

More of the item selection dialog followed: its two direction-key reads, where the first held key
of a group of four gives a step {x, y} ([#967](https://github.com/Yharnam-Hunters/Paleblood/pull/967)),
and its cursor move, list row, text pair copy and the status dialog's label `push_back`
([#995](https://github.com/Yharnam-Hunters/Paleblood/pull/995)). Both fixed vectors report a bad
slot or a full vector and then copy anyway, as the original does. In
[#1024](https://github.com/Yharnam-Hunters/Paleblood/pull/1024) the fixed vectors gained real
structs (sixteen slots, then an in-use count) in place of raw offsets, and a misalignment case
killed the last surviving mutant of the text pair copy. All six of these are now `verified`
([#1001](https://github.com/Yharnam-Hunters/Paleblood/pull/1001),
[#1017](https://github.com/Yharnam-Hunters/Paleblood/pull/1017),
[#1024](https://github.com/Yharnam-Hunters/Paleblood/pull/1024),
[#1053](https://github.com/Yharnam-Hunters/Paleblood/pull/1053)).

### The Scaleform glue

The menus are drawn with Scaleform, a Flash-based user interface library. The game's own code
around it is starting to be replaced ([#923](https://github.com/Yharnam-Hunters/Paleblood/pull/923)):

- the **heap adapter** that gives Scaleform its memory: free, and reallocate (which releases the old
  buffer even when the new allocation failed);
- two **work holder** destructors with the same code;
- the **movie screen**'s close and step (the player's step is tried up to twice; a failure closes
  the movie);
- the **texture repository**, which has a missing texture made by its owner's texture source;
- a **file set loader** of up to three files, its wait and build steps (both also match the
  original byte for byte with the original compiler);
- the release of a held **named resource**.

[@FFwLo](https://github.com/FFwLo) then replaced the named resource registry's find and release,
the resource handle's release and the `SprjScaleform` request queue
([#986](https://github.com/Yharnam-Hunters/Paleblood/pull/986)), and turned the 82 other compiled
copies of the named resource release, on two object layouts, into two
families ([#1151](https://github.com/Yharnam-Hunters/Paleblood/pull/1151)).
What each named resource is for is not known yet.

<details>
<summary>How these were verified</summary>

Each replacement is readable C++ in `game/menu/` and passes `verify.py` against the original on
constructed cases (the dialog object as a buffer, Dantelion's calls stubbed with the status
each case needs, invented text including every escaped character and its neighbours): 18, 18,
12 and 8 cases for the text input dialog's poll, close, open and destructor
([#727](https://github.com/Yharnam-Hunters/Paleblood/pull/727)), the entry copies in
[#823](https://github.com/Yharnam-Hunters/Paleblood/pull/823), and the later functions in
[#923](https://github.com/Yharnam-Hunters/Paleblood/pull/923),
[#967](https://github.com/Yharnam-Hunters/Paleblood/pull/967),
[#979](https://github.com/Yharnam-Hunters/Paleblood/pull/979),
[#986](https://github.com/Yharnam-Hunters/Paleblood/pull/986) and
[#995](https://github.com/Yharnam-Hunters/Paleblood/pull/995); each PR lists its case and
mutant counts. The six item dialog functions marked `verified` also pass on inputs recorded in
the game (for example 6 of 6 for the text pair copy, 8 of 8 for the label `push_back`), and an
independent review approved each one. The family members of #1151 run the named resource
release's own cases at each copy's address and are edge-verified. Everything else here is
edge-verified only.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x013a2a00` | 22 | `menu_named_resource_registry_find_013a2a00` | edge-verified | Scaleform glue: while the named resource registry is active and has its table, the table finds the entry by name (0x013abad0); null otherwise |
| `0x013a33c0` | 25 | `menu_named_resource_registry_release_013a33c0` | edge-verified | Scaleform glue: while the named resource registry is active and has its table, the table lets go of a non-null entry (0x013abd30) |
| `0x01401720` | 34 | `menu_named_resource_handle_release_01401720` | edge-verified | Scaleform glue: a resource handle with an owner is let go of by the owner (vtable slot 16), returning its answer; 0 without an owner |
| `0x01ed0dd0` | 203 | `menu_gaitem_select_move_cursor_01ed0dd0` | verified | GaitemSelectDialog cursor move: column +0x84 and list row clamped to the limits (0x01ed11f0, read twice); on change store, list set row, vtable +0x38 |
| `0x01ed7950` | 188 | `menu_gaitem_select_text_pair_copy_into_vector_01ed7950` | verified | GaitemSelectDialog text pair (two narrow strings, 0x70 B) copied into a fixed-vector slot; slot past the in-use count (+0x708) reported, still copied |
| `0x01edc580` | 197 | `menu_gaitem_select_list_set_row_01edc580` | verified | Item list set row (clamped; -1 when empty), vtable +8 scrollable, then scroll thumb placed and sized from the row and first shown row shares |
| `0x01edf560` | 254 | `menu_gaitem_select_entry_copy_into_pool` | edge-verified | GaitemSelectDialog list entry copy into a Dantelion fixed pool slot (0x88 B: int, word, wide string, word, wide string); reports a slot past the in-use count |
| `0x01ee6080` | 114 | `menu_gaitem_select_direction_keys_20_01ee6080` | verified | GaitemSelectDialog direction reader: first held of key assign keys 0x20-0x23 gives {x,y} = up/down/left, or right's state byte as x |
| `0x01ee61f0` | 134 | `menu_gaitem_select_direction_keys_24_01ee61f0` | verified | GaitemSelectDialog direction reader of keys 0x24-0x27; without a key assign skips up/down and asks left/right with null |
| `0x01f1bca0` | 235 | `menu_gaitem_status_entry_copy` | edge-verified | GaitemStatusDialog list entry copy of entries[index] (0x90 B: two words and wide strings, a ref-counted pointer at +0x80 (count +8 up), a byte) |
| `0x01f2c200` | 196 | `menu_gaitem_status_label_push_back_01f2c200` | verified | GaitemStatusDialog two-slot label vector push_back (kind byte, word, wide string; count at +0x98); full: reported, still appended |
| `0x0233ef00` | 175 | `menu_sprj_scaleform_queue_0233ef00` | edge-verified | SprjScaleform: unless told to skip, waits for and drops the pending job and hands the request to the player; then steps the request flags |
| `0x023411d0` | 116 | `menu_heap_adapter_free_023411d0` | edge-verified | Scaleform heap adapter free (vtable 0x057ab400): null kept; else DLNew release (thread release heap or the owner heap) |
| `0x02341330` | 188 | `menu_heap_adapter_reallocate_02341330` | edge-verified | Scaleform heap adapter reallocate: same size returns the buffer; else allocate from heaps[index] and copy the smaller size; DLNew release of the old one |
| `0x023548d0` | 162 | `menu_movie_screen_close_023548d0` | edge-verified | movie screen close: with the menu state flush the SprjScaleform player (if any) and close the movie; then queue the request |
| `0x02354d90` | 213 | `menu_movie_screen_step_02354d90` | edge-verified | movie screen step: the player step up to twice (+0xd0 counts tries); no player or a failed step marks the menu state and closes the movie; queue the request |
| `0x02360d80` | 168 | `menu_tex_repository_require_02360d80` | edge-verified | ScaleformTexRepository: a missing key gets a texture from the owner source (+0x940; vtable slot 0x130) |
| `0x02363c00` | 128 | `menu_work_holder_destroy_02363c00` | edge-verified | work holder destructor (vtable 0x05756bd0): base vtable; one user less (last sets the signal at +0x10); one reference less |
| `0x02363d20` | 128 | `menu_work_holder_destroy_02363d20` | edge-verified | work holder destructor (vtable 0x05756c00): the same code as 0x02363c00 |
| `0x02387050` | 144 | `menu_file_set_loader_wait_02387050` | edge-verified | file set loader step: copy the sizes of the loaded files (state 4); one still loading stops it; then the next step and step done |
| `0x02387650` | 164 | `menu_file_set_loader_build_02387650` | edge-verified | file set loader step: build from the first and third files with the name; 0 declined (+0x19a); other than 1 failed (+0x15b); next step and step done |
| `0x0238a210` | 166 | `menu_named_resource_release_0238a210` | edge-verified | named resource release when held: handle released; registry entry found by the wide name and released; name emptied; loaded cleared |
| `0x02415fe0` | 89 | `menu_text_input_destroy` | edge-verified | Text input dialog destructor: closes it, frees the text, destroys the Dantelion dialog object |
| `0x02416040` | 574 | `menu_text_input_close` | edge-verified | Text input dialog: waits for an open dialog to end, then empties its text and resets the state |
| `0x024162a0` | 94 | `menu_text_input_open` | edge-verified | Text input dialog: opens the system keyboard dialog with the given settings once no other is open |
| `0x02416300` | 482 | `menu_text_input_update` | edge-verified | Text input dialog per-frame poll: takes a finished dialog's text (markup characters swapped for glyphs) and how it ended |
| `0x02d03420` | 46 | `menu_system_text_dialog_status_02d03420` | edge-verified | Dantelion text dialog: the dialog backend's status while the system dialog is open, -1 otherwise (the backend made on first use) |
| `0x02d03450` | 60 | `menu_system_text_dialog_open_02d03450` | edge-verified | Dantelion text dialog: unless open, asks the dialog backend to open the system dialog with the settings and records the answer as the open flag |
| `0x02d03490` | 105 | `menu_system_text_dialog_take_text_02d03490` | edge-verified | Dantelion text dialog: once the backend's status is 2 or 3, closes the object and asks the backend for the text; false while closed or running |
| `0x02d03500` | 26 | `menu_system_text_dialog_set_up_02d03500` | edge-verified | Dantelion text dialog: unless the system dialog is open, copies the settings into the object (0x02d03520) and returns true |

## Structs and globals

**Dantelion's dialog status** (`0x02d03420`, replaced in #979): -1 when no dialog is open, 0 while
it runs, 3 for accepted text, and 2 for a cancel, an abort or a failed result read. That the two
system calls it reads are the system dialog's status and result calls is inferred from their
signatures.

## Patch points

No patch points are recorded for these functions.

## Open questions

- The menus themselves: the Scaleform screens, the message repository and the code that drives
  each menu.
- What the named resources and the file set loader's files are, and which menus use them.
- Which caller opens the text input dialog for each use (the hunter's name, messages, the debug
  menu).
