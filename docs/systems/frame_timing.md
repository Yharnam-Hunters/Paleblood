<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Frame timing

How the game paces its frames: a frame step that runs once a frame, a frame limiter that waits out each frame's budget, and a task manager that runs the frame's work.

**At a glance**

- **Scope:** Frame pacing, delta time, vsync and the main loop's time step.
- **Verified:** The frame step, the frame limiter, the flipper constructor and the task manager's frame path are replaced and verified.
- **Our option:** `BB_TARGET_FPS` does in code what the community frame-rate patches do in these functions.
- **Open:** A few limiter fields, the owner of the `0x0111a...` clock layer, and most functions the 60 FPS patches edit.

## Summary

### The frame step

`frame_timing_frame_step` (`0x02418d20`) runs once per frame. In order, it:

1. checks that the `SprjWindow` is running;
2. creates the `SprjFlipper` singleton on first use (`frame_timing_flipper_init`, `0x02434520`);
3. runs the frame limiter, `frame_timing_pace_frame` (`0x02434770`), a method of that flipper;
4. runs the task update with a small 1/30 s descriptor;
5. asks a quit gate and a quit query, and returns whether the game keeps running.

Both the limiter and the frame step are replaced and verified.

### The frame limiter

Each frame the limiter does four things.

**It applies the flip mode.** The constructor reads the mode from the config value
`Game.FlipMode`. A pending mode at `+0xc` replaces the current one at `+0x8` when `+0x276` is
set. The mode chooses the sync interval (stored at `+0x10`) and the frame interval (a float at
`+0x18`):

| Flip mode | Sync interval | Frame interval |
|---|---|---|
| 0 and 1 | 2 | 1/30 s |
| 2 | 1 | 1/60 s |
| 3 and 4 | 1 | 1/30 s |

**It reads the time** with `gettimeofday`, in microseconds.

**It waits out the frame's budget**, shortened when recent frames ran late. A switch in the build
(`0x013e3980`, always 1) selects the spinning wait, which polls the clock without sleeping
(hundreds of thousands of reads in a long frame). A sleeping wait is compiled in but unused.

**It keeps the last 32 frame times** in a ring (index at `+0x260`) and derives a frame rate from
their average (`+0x2b8`).

### Which clock the game reads

`gettimeofday` supplies wall time, so it is not guaranteed to be monotonic. The frame limiter's
confirmed call passes a null timezone pointer. The game's separate verified clock helper invokes
`sceKernelClockGettime` with ID `4`, identified as `CLOCK_MONOTONIC`; that does not establish the
IDs accepted by the imported libc `clock_gettime` wrapper. See the
runtime system-call notes.

Two other clock functions are easy to mistake for part of the limiter:

- `0x004632d0` is a small `gettimeofday` helper in microseconds that the stock game never calls:
  nothing calls it directly, and no relocated pointer (vtable or table slot) points at it. The
  frame-rate patches overwrite it and its neighbours with their own code (a code cave), so it is
  not replaced.
- The millisecond clock `frame_timing_get_monotonic_ms` (`0x0111a7f0`), its microsecond twin
  `0x0111a760` and the sleep wrapper `0x0111a880` (now replaced, see below) are not on the limiter's path. They look like
  the timing layer of a separate subsystem.

<details>
<summary>Correction (2026-10-06): the 0x0111a... clocks are a separate layer</summary>

The millisecond clock `frame_timing_get_monotonic_ms` (`0x0111a7f0`), its microsecond twin
`0x0111a760` and the sleep wrapper `0x0111a880` are not on the limiter's path. All 52 of
their callers sit in one region (`0x0110e...`-`0x0121b...`) that shares its own helpers, and
no frame-rate patch touches any of them: they look like the timing layer of a separate
subsystem (unidentified; possibly middleware). Their system label may move once that
subsystem is known.

</details>

### The fixed-step state methods

Many small objects in the game advance in fixed steps of 1/30 s, whatever the real frame time is.
The community "60 FPS++" patch changes those steps to 1/60 s, so knowing exactly which functions it
edits, and what each belongs to, is part of running the game faster than 30 FPS. Paleblood has
replaced a growing set of them in `game/frame_timing/fixed_step_states.cpp`.

The latest belong to the **Chalice Dungeon gate**, the object behind the ritual altar:

- **Its steps have names.** The gate (`SprjDungeonGateIns`) keeps a table of 24 steps whose names
  are in the executable. They give its states their meaning: loading a channel, no chalice, a local
  or online ritual, a local or online dungeon, and an error display.
- **Finding the gate.** The game finds the current gate through `SprjHolygrail`. The highest of
  seven event flags, 9020 to 9026, picks one of eight gate slots; with none set, it uses the last
  slot.
- **Waiting for it.** Two methods advance the gate while it is busy and are done once it is idle.
  Two more wait until it is idle in a dungeon mode, or in a ritual mode.
- **Leaving a mode.** One method, held by an object that points to the gate, takes the gate out of
  a ritual or dungeon mode (request 7) and out of the error mode (request 10), around a check on
  the gate that reads the online manager, and reports when the check passes in the online dungeon
  mode. What its caller does with that answer is not known: the caller ignores it.
- **The check itself.** It passes only when the gate is idle in mode 1 (no Holy Chalice), four
  bytes of the online manager (`FrpgNetMan`) allow it (one set, three clear; what they mean is not
  known), and the id it is given is set. It then looks up an object for that id and keeps it as
  the gate's handle, a reference-counted pointer: the engine counts how many holders an object has
  and destroys it when the last one lets go, so swapping the handle drops the old object's count
  and takes one on the new. Finally, if the gate is still idle, it gives the gate request 6 and
  reports success. That the id names an online session is a guess.
- **Its sibling, the entry check.** It opens with the same gates, then searches a list for the
  first entry with a given key that holds an object, and keeps that object as the gate's handle in
  the same way, with request 4 instead of 6. When the gate becomes busy while the old handle is let
  go, it stops and reports failure rather than trying the next entry.
- **Letting the handle go again.** In a ritual mode a third method resets the handle. In the
  local ritual mode it always may; in the server ritual mode only when the online manager's bytes
  allow it and a flag bit in the handled object is clear. It clears one word of that object, hands
  the handle to a routine not yet analyzed (`0x01e9cb80`), and gives the gate request 8.
  A fourth method does the same under the same rules but first gathers the gate's sixteen slots:
  each active one goes into a short list (its key, its number, and a value the caller supplies for
  it), built on one of the engine's memory heaps. With no active slot it does nothing; otherwise
  it hands the handle and the list on (`0x01e9cd20`, not yet analyzed), gives request 8, and then
  frees the list's memory.
- **Passing the handle on.** A readiness check lets the gate go on when it is idle in a ritual or
  dungeon mode and the online manager allows it; in the server modes the handled object must also
  have one word at zero. Two methods that differ only in a "kind" number (1 or 2) run that check,
  clear two words of the handled object, hand the handle on with the kind (to one routine in the
  local modes, another in the server modes, neither analyzed yet), and give request 9.
- **Creating the handle.** Two methods make the handle in the first place, both only while the
  gate is idle in mode 1. One drops the old handle, builds a new handled object from two blocks of
  data, a block of the online manager and a name (the current name entry's, or a default), and
  gives request 3. The other reads four "levels" from event flags: for each of four base flags, the
  highest level from 9 down to 1 whose flag is set (flag numbers 6221 to 6287, every seventh one
  belonging to the same column). It builds the handled object from those four levels and gives
  request 5. What the levels and the flags stand for in the game is not known yet.

The millisecond sleep wrapper (`0x0111a880`), which is not on the limiter's path, is replaced too.
It converts the engine's millisecond delay to the kernel's microseconds. It multiplies in 32 bits,
as the original does, so very large delays wrap, and it always reports success.

<details>
<summary>Fixed-step methods: PRs, contributors and evidence</summary>

- [#104](https://github.com/Yharnam-Hunters/Paleblood/pull/104) by
  [@evill33t](https://github.com/evill33t): the millisecond sleep wrapper.
- [#138](https://github.com/Yharnam-Hunters/Paleblood/pull/138): five `SprjDungeonGateIns`
  methods that step the gate's state (`0x01eb5390`, `0x01eb5490`, and the event handlers at
  `0x01eb9020`, `0x01ebb090`, `0x01ebf060`).
- [#141](https://github.com/Yharnam-Hunters/Paleblood/pull/141): four methods that find the
  gate through `SprjHolygrail` (`0x0200f970`, `0x020118f0`, `0x01f03210`, `0x01f03680`).
- [#182](https://github.com/Yharnam-Hunters/Paleblood/pull/182): the method that leaves the
  gate's modes around the check `0x01ea5280` (`0x01ec3ef0`, eleven 60 FPS++ edits).
- [#186](https://github.com/Yharnam-Hunters/Paleblood/pull/186): the check itself
  (`0x01ea5280`, one 60 FPS++ edit), using the engine's shared reference counting from
  [#185](https://github.com/Yharnam-Hunters/Paleblood/pull/185).
- [#188](https://github.com/Yharnam-Hunters/Paleblood/pull/188): the entry check (`0x01ea4a10`,
  one 60 FPS++ edit). Part of it sits in bytes the decompiler leaves out after an error report;
  see [Ghidra & PS4 Quirks](../../QUIRKS.md).
- [#192](https://github.com/Yharnam-Hunters/Paleblood/pull/192): the ritual handle reset
  (`0x01ea5670`, one 60 FPS++ edit).
- [#194](https://github.com/Yharnam-Hunters/Paleblood/pull/194): the ritual slot reset
  (`0x01ea5850`, one 60 FPS++ edit).
- [#196](https://github.com/Yharnam-Hunters/Paleblood/pull/196): the readiness check
  (`0x01ea5c90`) and the two pass-on methods (`0x01ea5e20`, `0x01ea6080`; one 60 FPS++ edit
  each).
- [#198](https://github.com/Yharnam-Hunters/Paleblood/pull/198): the handle creation
  (`0x01ea47a0`, one 60 FPS++ edit).
- [#199](https://github.com/Yharnam-Hunters/Paleblood/pull/199): the level creation
  (`0x01ea4cb0`, one 60 FPS++ edit), with the event-flag store shared with the event system.
- Status: **replaced**, not verified. Each was compared with the original function on authored
  edge cases, stock and with the 60 FPS++ edits, and wrong variants were rejected. None was
  checked on frames recorded from a real game run.

</details>

### Frame time in game logic

The frame step also passes time into character updates. One verified update accumulates a
per-frame points value using that time and retains fractional progress between frames. Its
evidence and unresolved stat identity are documented in
Stats & Progression.

### The task manager's frame

Each frame, the frame step hands the frame's work to `FD4::FD4TaskManager` (singleton
`0x058b2e30`). The path is a good example of how an engine brackets its work so that nothing runs
twice or half-finished:

1. `0x024512a0` fetches the task manager and runs every task group (`0x01388c60`, mask `-1`).
2. `0x01388c70` refuses to start while a frame is already running (a busy flag at `+0x38`). It
   flushes the task queue that collected work since the last frame (`0x0143e490`: lock, run each
   queued object, destroy and free it, empty the list, unlock), steps the worker threads' queues
   (`0x014400a0`), and publishes the frame's value (`0x0143f9f0` copies a float from the frame's
   information to a global).
3. With the busy flag set, it calls the dispatcher (`+0x40`, virtual `+0x30`), which runs the
   frame's tasks; then it clears the flag, flushes the second queue and steps the workers again.

The dispatcher (`0x0138a370`) is the last link: when its group record asks for it, it wakes
every registered task (76 in Iosefka's Clinic), then fills a job with the record, the task table and
the group mask and runs it through the same five steps as the workers. Each of those tasks is a
piece of the game's own per-frame work, the next layer to map.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x004632d0` | 67 | `frame_timing_get_time_us` | original | dead in the stock game: only unwind entries refer to it; the community frame-rate patches overwrite it as a code cave (gettimeofday in microseconds) |
| `0x0111a7f0` | 131 | `frame_timing_get_monotonic_ms` | verified | Milliseconds since its first call from CLOCK_MONOTONIC; keeps the first seconds in the clock state object |
| `0x0111a880` | 19 | `frame_timing_sleep_ms` | edge-verified | Convert milliseconds to microseconds with uint32 wrapping; always call the kernel and discard its result |
| `0x01388c60` | 16 | `frame_timing_task_run_all_01388c60` | verified | task manager: run the tasks of every group (0x01388c70 with -1) |
| `0x01388c70` | 171 | `frame_timing_task_run_01388c70` | verified | task manager run: prepare the frame, dispatch through +0x40 (virtual +0x30) with +0x38 set, finish |
| `0x0138a370` | 330 | `frame_timing_task_dispatch_0138a370` | verified | task dispatcher (virtual +0x30): wakes registered tasks, updates the group record, runs the job like the worker step |
| `0x013d3520` | 87 | `frame_timing_task_013d3520` | verified | per-frame task: passes the frame-time descriptor and its seconds to three parts; Uncap FPS++ edits it |
| `0x0143e490` | 174 | `frame_timing_task_flush_queue_0143e490` | verified | task manager queue flush: lock, run, destroy and free every queued object, empty the list, unlock |
| `0x0143f9f0` | 18 | `frame_timing_task_set_frame_value_0143f9f0` | verified | task manager: copies the float at +0x8 of the frame information to the global 0x058b7e08 |
| `0x014400a0` | 180 | `frame_timing_task_workers_step_014400a0` | verified | task manager workers: three rounds over the worker queues +0x58, +0x68, +0x60 |
| `0x01ea47a0` | 611 | `frame_timing_gate_create_handle` | edge-verified | handle creation of the Chalice Dungeon gate state: idle in mode 1, drops the handle, creates one from two filled blocks, FrpgNetMan+0xa68 and a name, adopts it, request 3 |
| `0x01ea4a10` | 501 | `frame_timing_gate_entry_check` | edge-verified | entry check of the Chalice Dungeon gate state: online gates as 0x01ea5280; keeps the first keyed entry's object as its handle, then request 4 |
| `0x01ea4cb0` | 1472 | `frame_timing_gate_create_handle_from_levels` | edge-verified | level creation of the Chalice Dungeon gate state: online gates; four highest set levels (event flags 0x184d+i+7*level), a handle object from them, adopted; request 5 |
| `0x01ea5280` | 553 | `frame_timing_gate_owner_check` | edge-verified | owner check of the Chalice Dungeon gate state: idle in mode 1, FrpgNetMan flags and an id allow it; keeps the object for the id as its handle, then request 6 |
| `0x01ea5670` | 451 | `frame_timing_gate_ritual_handle_reset` | edge-verified | ritual handle reset of the Chalice Dungeon gate state: in a ritual mode (server: online gates, flag bit clear) clears the handle's +0x124, rebuilds it, then request 8 |
| `0x01ea5850` | 708 | `frame_timing_gate_ritual_slot_reset` | edge-verified | ritual slot reset of the Chalice Dungeon gate state: as 0x01ea5670, but lists the active slots on the container heap and passes them on with the handle |
| `0x01ea5c90` | 354 | `frame_timing_gate_ritual_or_dungeon_ready` | edge-verified | readiness check of the Chalice Dungeon gate state: idle in a local ritual/dungeon mode with FrpgNetMan's bytes allowing it, or a server one also needing a handle whose +0x50 is zero |
| `0x01ea5e20` | 546 | `frame_timing_gate_pass_on_kind_1` | edge-verified | when ready (0x01ea5c90): clears the handle's +0x11c and +0x120, passes it on for the mode with kind 1 if its +0x50 is zero, then request 9 |
| `0x01ea6080` | 546 | `frame_timing_gate_pass_on_kind_2` | edge-verified | as 0x01ea5e20 with kind 2 |
| `0x01eb5390` | 182 | `frame_timing_dungeon_gate_step_init_for_channel_load` | edge-verified | SprjDungeonGateIns::STEP_Init_forChannelLoad: when the gate state is idle in ChannelLoad, request 2 and advance it one fixed step (1/30 s; 1/60 s in the 60 FPS patches) |
| `0x01eb5490` | 246 | `frame_timing_dungeon_gate_step_mode_switch` | edge-verified | SprjDungeonGateIns::STEP_ModeSwitch: advance the gate state one fixed step while busy, then go to the first step of its mode |
| `0x01eb9020` | 303 | `frame_timing_dungeon_gate_on_event_0` | edge-verified | SprjDungeonGateIns handler of event 0: request 1, then request 2, each advancing the gate state one fixed step |
| `0x01ebb090` | 184 | `frame_timing_dungeon_gate_on_event_1000` | edge-verified | SprjDungeonGateIns handler of event 1000: when idle in states 2..5, request 7 and advance the gate state one fixed step |
| `0x01ebf060` | 176 | `frame_timing_dungeon_gate_on_event_3000` | edge-verified | SprjDungeonGateIns handler of event 3000: when idle, request 10 and advance the gate state one fixed step |
| `0x01ec3ef0` | 795 | `frame_timing_leave_modes_around_owner_check_01ec3ef0` | edge-verified | state method on the Chalice Dungeon gate: leaves ritual/dungeon modes (request 7), the error mode (request 10); true when check 0x01ea5280 says yes in server dungeon mode |
| `0x01f03210` | 307 | `frame_timing_holygrail_wait_for_dungeon_mode` | edge-verified | state method: advance the current Chalice Dungeon gate (event flags 9020..9026 pick it) one fixed step while busy, then report whether it is in a dungeon mode; not reached by the routes yet |
| `0x01f03680` | 296 | `frame_timing_holygrail_wait_for_ritual_mode` | edge-verified | state method: advance the current Chalice Dungeon gate one fixed step while busy, then report whether it is in a ritual mode; not reached by the routes yet |
| `0x01f6a9f0` | 121 | `frame_timing_until_idle_01f6a9f0` | edge-verified | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x0200d8a0` | 137 | `frame_timing_until_idle_0200d8a0` | edge-verified | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x0200e100` | 121 | `frame_timing_until_idle_0200e100` | edge-verified | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x0200e270` | 151 | `frame_timing_request_7_0200e270` | edge-verified | state method: when idle in states 2..5, request 7 and advance the owner one fixed step (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x0200f970` | 283 | `frame_timing_holygrail_until_idle_0200f970` | edge-verified | state method: advance the current Chalice Dungeon gate one fixed step while busy; done once idle; not reached by the routes yet |
| `0x020118f0` | 283 | `frame_timing_holygrail_until_idle_020118f0` | edge-verified | state method: advance the current Chalice Dungeon gate one fixed step while busy; done once idle; not reached by the routes yet |
| `0x02012610` | 121 | `frame_timing_until_idle_02012610` | edge-verified | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x02012780` | 122 | `frame_timing_request_10_02012780` | edge-verified | state method: when idle, request 10 and advance the owner one fixed step (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x020128f0` | 121 | `frame_timing_until_idle_020128f0` | edge-verified | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x02012a60` | 151 | `frame_timing_request_7_02012a60` | edge-verified | state method: when idle in states 2..5, request 7 and advance the owner one fixed step (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x02012bf0` | 121 | `frame_timing_until_idle_02012bf0` | edge-verified | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (the Chalice Dungeon gate's state, at +0x108 in SprjDungeonGateIns; not reached by the routes yet) |
| `0x02418d20` | 467 | `frame_timing_frame_step` | verified | Per frame: SprjWindow running check; creates SprjFlipper; calls the limiter; runs the task update; returns running and not quitting |
| `0x02434520` | 552 | `frame_timing_flipper_init` | verified | SprjFlipper constructor: reads Game.FlipMode (default 4; pending default 3; clamped to 4); unset in the shipped config |
| `0x02434770` | 2077 | `frame_timing_pace_frame` | verified | Frame limiter: target interval at +0x18 (1/30 or 1/60 s); spins out the frame (the sleeping wait is compiled in but unused); 32-frame ring; every FPS patch edits it |
| `0x024512a0` | 78 | `frame_timing_task_frame_024512a0` | verified | task manager frame entry from the frame step: FD4TaskManager singleton, then 0x01388c60 |

## Structs and globals

- Clock state object, reached through the pointer at `0x056d6ae8`. Field `+0x10`: the
  seconds of the first clock call (32-bit), written once by the first call of either clock
  wrapper and read by every later call. The rest of the object is not documented.

The `SprjFlipper` fields named on this page, gathered in one place:

| Offset | Meaning |
|---|---|
| `+0x8` | Current flip mode |
| `+0xc` | Pending flip mode |
| `+0x10` | Sync interval |
| `+0x14` | Unknown |
| `+0x18` | Frame interval (float) |
| `+0x260` | Index into the ring of recent frame times |
| `+0x264` | Measured frame time |
| `+0x268`, `+0x26c` | Set how many recent late frames the catch-up sums (meaning open); the patches zero the vblank word at `+0x268` |
| `+0x271`, `+0x272` | Clear flags |
| `+0x276` | Apply the pending flip mode |
| `+0x2b8` | Frame rate derived from the ring's average |
| `+0x2bc`, `+0x2c0` | Override counters |
| `+0x2c4` | Reset flag |

## Patch points

The community frame-rate patches (Kyo; Lance McDonald and Kyo for `Uncap FPS++`) edit three of
our functions; disassembling the limiter with and without each patch shows exactly what they do:

- **Limiter** (`0x02434770`): the flip-mode switch always takes one entry (mode 2 for 30 and 60,
  mode 0 for uncapped) with patched values: interval 1/30 s (30), 1/60 s (60) or 1/240 s
  (uncapped), and a zero vblank word (`+0x268`). Every override is skipped: the reset flag
  (`+0x2c4`), the two override counters (`+0x2bc`, `+0x2c0`) and the two clear flags (`+0x271`,
  `+0x272`). At the end the sync interval (`+0x10`) is kept on late frames instead of zeroed.
- **Frame step** (`0x02418d20`): the frame time passed to the task update starts at 1/60 s (30:
  1/30 s), and when the flipper's interval is not 1/30 s it becomes the measured frame time
  (`+0x264`) clamped to [interval, 1/30 s]. This is the patches' delta time: the game logic
  advances by how long the frame really took. The patch drops the SprjTask check to make room.
- **Flipper constructor** (`0x02434520`): the first interval is 1/60 s (60 and 90).
- They also edit 59 other functions; 12 of them are dead code in the stock game (no caller, no
  relocated pointer). Many of the rest pass the same 1/30 s frame-time descriptor to an owner
  object, which the 60 FPS patches change to 1/60 s.

The patches also rewrite dozens of shared constants in the data segment (the 1/30 s step at
`0x04d29170` becomes 1/60 s, 1/90 s or, uncapped, 1/60 s), which code we have not replaced reads
too. `BB_TARGET_FPS` covers only the replaced functions, so it is meant to run together with
the patch; option checks run both sides on the patched image for that reason.

<details>
<summary>How the patch edits were mapped</summary>

`tools/patch_overlap.py` maps every patch line to the function it falls in, and lists the
patches that do nothing while our hooks are installed.

</details>

## Our option: BB_TARGET_FPS

`BB_TARGET_FPS=30`, `60` or `uncapped` does in code what `30 FPS++`, `60 FPS++` and
`Uncap FPS++` do in the three functions above. Unset keeps the original behaviour exactly.

The option has to cover the frame step as well as the limiter. A faster limiter alone would
still tell the game logic that every frame lasts 1/30 s; the delta time comes from the frame
step (see the patch points above).

The other functions the patches edit are not replaced yet, so a 60 FPS game would still need the
community patch for them; its edits inside our three functions do not run, and our option does
their part.

A second option, `BB_LIMITER_WAIT=sleep`, makes the limiter use the sleeping wait the game
carries but never selects: it sleeps until about 5 ms before the frame's end and spins the
rest, so a fast machine does not keep a core busy waiting.

<details>
<summary>How the option is checked against the patches</summary>

Each part is checked against the patch itself: `tools/verify.py run --patch FILE:NAME --env
BB_TARGET_FPS=...` runs the original with the patch's bytes applied against our replacement
with the option, on the edge cases and on frames recorded in the game at 60 FPS.

</details>

<details>
<summary>Recorded frame times at 60 FPS (October 7)</summary>

A saved October 7 measurement with the external host scaffold, all replacements hooked, and
`BB_TARGET_FPS=60` reports a median frame time of 16.67 ms and p99 of 16.69 ms. <!-- number:ok -->
That historical result is recorded in [Paleblood's status evidence](https://github.com/Yharnam-Hunters/Paleblood/blob/af97b90bb1e88853adfc04b8da2b3d7506516565/STATUS.md#recent-results);
it is not a real-display result from Paleblood's project-owned boot path.

</details>

<details>
<summary>History: the first version changed only the limiter</summary>

An earlier version only changed the limiter. Because the hooked frame step no longer ran the
patches' delta-time code, at 60 FPS the game logic would still have been told every frame
lasts 1/30 s; checking against the patches found that, and that the limiter's own settings
differed.

</details>

## Open questions

- What `+0x14` and the counters at `+0x268` / `+0x26c` mean (the latter set how many recent
  late frames the catch-up sums).
- Which subsystem the `0x0111a...` clock layer belongs to.
- What each live function the 60 FPS patches edit belongs to, and which run in gameplay.
