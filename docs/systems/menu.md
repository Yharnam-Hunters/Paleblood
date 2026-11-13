<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Menu

The game's menus and dialogs: so far the text input dialog behind the system keyboard, and two item dialogs' list entries.

**At a glance**

- **Scope:** Paleblood's `game/menu/`: the text input dialog (open, poll, close, destructor) and the list entry copies of two item dialogs.
- **Verified:** All six replacements are edge-verified against constructed cases. None is `verified` yet: none has been checked on inputs recorded from a real game run.
- **Open:** The menus themselves (the Scaleform screens and the code that drives them) are not mapped yet.

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
new system. Dantelion's own dialog calls under them stay original. In coverage boots the poll ran
thousands of times and close a few times; open and the destructor did not run.

### Item dialog list entries

Two item dialogs copy their list entries, each two wide strings with a word before each:
`0x01edf560` copies an entry of the item selection dialog (`GaitemSelectDialog`) into a slot of a
fixed pool, and `0x01f1bca0` copies entry *n* of the item status dialog (`GaitemStatusDialog`)
([#823](https://github.com/Yharnam-Hunters/Paleblood/pull/823)). Both are edge-verified.

<details>
<summary>How these were verified</summary>

Each replacement is readable C++ in `game/menu/` and passes `verify.py` against the original on
constructed cases (the dialog object as a buffer, Dantelion's calls stubbed with the status
each case needs, invented text including every escaped character and its neighbours): 18, 18,
12 and 8 cases for the text input dialog's poll, close, open and destructor
([#727](https://github.com/Yharnam-Hunters/Paleblood/pull/727)), and the entry copies in
[#823](https://github.com/Yharnam-Hunters/Paleblood/pull/823). None has been checked on inputs
recorded from a real game run, so none is `verified`.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x01edf560` | 254 | `menu_gaitem_select_entry_copy_into_pool` | edge-verified | GaitemSelectDialog list entry copy into a Dantelion fixed pool slot (0x88 B: int, word, wide string, word, wide string); reports a slot past the in-use count |
| `0x01f1bca0` | 235 | `menu_gaitem_status_entry_copy` | edge-verified | GaitemStatusDialog list entry copy of entries[index] (0x90 B: two words and wide strings, a ref-counted pointer at +0x80 (count +8 up), a byte) |
| `0x02415fe0` | 89 | `menu_text_input_destroy` | edge-verified | Text input dialog destructor: closes it, frees the text, destroys the Dantelion dialog object |
| `0x02416040` | 574 | `menu_text_input_close` | edge-verified | Text input dialog: waits for an open dialog to end, then empties its text and resets the state |
| `0x024162a0` | 94 | `menu_text_input_open` | edge-verified | Text input dialog: opens the system keyboard dialog with the given settings once no other is open |
| `0x02416300` | 482 | `menu_text_input_update` | edge-verified | Text input dialog per-frame poll: takes a finished dialog's text (markup characters swapped for glyphs) and how it ended |

## Structs and globals

**Dantelion's dialog status** (`0x02d03420`): -1 when no dialog is open, 0 while it runs, 3 for
accepted text, and 2 for a cancel, an abort or a failed result read. That the two system calls it
reads are the system dialog's status and result calls is inferred from their signatures.

## Patch points

No patch points are recorded for these functions.

## Open questions

- The menus themselves: the Scaleform screens, the message repository and the code that drives
  each menu.
- Which caller opens the text input dialog for each use (the hunter's name, messages, the debug
  menu).
