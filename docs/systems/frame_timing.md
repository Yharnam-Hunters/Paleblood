<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Frame timing

## Summary

Scope: frame pacing, delta time, vsync and the main loop's time step.

**The frame limiter** is `frame_timing_pace_frame` (`0x02434770`), a method of the
`SprjFlipper` singleton, called once per frame from `frame_timing_frame_step` (`0x02418d20`).
The frame step checks that the `SprjWindow` is running, creates the flipper on first use
(`frame_timing_flipper_init`, `0x02434520`), runs the limiter, then the task update with a
small 1/30 s descriptor, asks a quit gate and a quit query, and returns whether the game keeps
running. Both the limiter and the frame step are replaced and verified.

The limiter:

- applies the flip mode, read from the config value `Game.FlipMode` by the constructor (a
  pending mode at `+0xc` replaces the current one at `+0x8` when `+0x276` is set). Modes 0 and
  1: sync interval 2, 1/30 s; mode 2: sync interval 1, 1/60 s; modes 3 and 4: sync interval 1,
  1/30 s. The frame interval is a float at `+0x18`, the sync interval at `+0x10`;
- reads the time with `gettimeofday` in microseconds;
- waits out the frame's budget, shortened when recent frames ran late. A switch in the build
  (`0x013e3980`, always 1) selects the spinning wait, which polls the clock without sleeping
  (hundreds of thousands of reads in a long frame); a sleeping wait is compiled in but unused;
- keeps the last 32 frame times in a ring (index at `+0x260`) and derives a frame rate from
  their average (`+0x2b8`).

`0x004632d0` is a small `gettimeofday` helper in microseconds that the stock game never calls:
nothing calls it directly, and no relocated pointer (vtable or table slot) points at it. The
frame-rate patches overwrite it and its neighbours with their own code (a code cave), so it is
not replaced.

**Correction (2026-10-06).** The millisecond clock `frame_timing_get_monotonic_ms`
(`0x0111a7f0`), its microsecond twin `0x0111a760` and the sleep wrapper `0x0111a880` are not on
the limiter's path. All 52 of their callers sit in one region (`0x0110e...`-`0x0121b...`) that
shares its own helpers, and no frame-rate patch touches any of them: they look like the timing
layer of a separate subsystem (unidentified; possibly middleware). Their system label may move
once that subsystem is known.

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
| `0x01388c60` | 16 | `frame_timing_task_run_all_01388c60` | verified | task manager: run the tasks of every group (0x01388c70 with -1) |
| `0x01388c70` | 171 | `frame_timing_task_run_01388c70` | verified | task manager run: prepare the frame, dispatch through +0x40 (virtual +0x30) with +0x38 set, finish |
| `0x0138a370` | 330 | `frame_timing_task_dispatch_0138a370` | verified | task dispatcher (virtual +0x30): wakes registered tasks, updates the group record, runs the job like the worker step |
| `0x013d3520` | 87 | `frame_timing_task_013d3520` | verified | per-frame task: passes the frame-time descriptor and its seconds to three parts; Uncap FPS++ edits it |
| `0x0143e490` | 174 | `frame_timing_task_flush_queue_0143e490` | verified | task manager queue flush: lock, run, destroy and free every queued object, empty the list, unlock |
| `0x0143f9f0` | 18 | `frame_timing_task_set_frame_value_0143f9f0` | verified | task manager: copies the float at +0x8 of the frame information to the global 0x058b7e08 |
| `0x014400a0` | 180 | `frame_timing_task_workers_step_014400a0` | verified | task manager workers: three rounds over the worker queues +0x58, +0x68, +0x60 |
| `0x01f6a9f0` | 121 | `frame_timing_until_idle_01f6a9f0` | replaced | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x0200d8a0` | 137 | `frame_timing_until_idle_0200d8a0` | replaced | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x0200e100` | 121 | `frame_timing_until_idle_0200e100` | replaced | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x0200e270` | 151 | `frame_timing_request_7_0200e270` | replaced | state method: when idle in states 2..5, request 7 and advance the owner one fixed step (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x02012610` | 121 | `frame_timing_until_idle_02012610` | replaced | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x02012780` | 122 | `frame_timing_request_10_02012780` | replaced | state method: when idle, request 10 and advance the owner one fixed step (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x020128f0` | 121 | `frame_timing_until_idle_020128f0` | replaced | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x02012a60` | 151 | `frame_timing_request_7_02012a60` | replaced | state method: when idle in states 2..5, request 7 and advance the owner one fixed step (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x02012bf0` | 121 | `frame_timing_until_idle_02012bf0` | replaced | state method: advance the owner one fixed step while busy (1/30 s; 1/60 s in the 60 FPS patches) (a Chalice Dungeon state machine: built next to SprjHolygrail; not reached by the routes yet) |
| `0x02418d20` | 467 | `frame_timing_frame_step` | verified | Per frame: SprjWindow running check; creates SprjFlipper; calls the limiter; runs the task update; returns running and not quitting |
| `0x02434520` | 552 | `frame_timing_flipper_init` | verified | SprjFlipper constructor: reads Game.FlipMode (default 4; pending default 3; clamped to 4); unset in the shipped config |
| `0x02434770` | 2077 | `frame_timing_pace_frame` | verified | Frame limiter: target interval at +0x18 (1/30 or 1/60 s); sleeps then spins; 32-frame ring; every FPS patch edits it |
| `0x024512a0` | 78 | `frame_timing_task_frame_024512a0` | verified | task manager frame entry from the frame step: FD4TaskManager singleton, then 0x01388c60 |
## Structs and globals

- Clock state object, reached through the pointer at `0x056d6ae8`. Field `+0x10`: the
  seconds of the first clock call (32-bit), written once by the first call of either clock
  wrapper and read by every later call. The rest of the object is not documented.

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

`tools/patch_overlap.py` maps every patch line to the function it falls in, and lists the
patches that do nothing while our hooks are installed.

## Our option: BB_TARGET_FPS

`BB_TARGET_FPS=30`, `60` or `uncapped` does in code what `30 FPS++`, `60 FPS++` and
`Uncap FPS++` do in the three functions above. Unset keeps the original behaviour exactly.

Each part is checked against the patch itself: `tools/verify.py run --patch FILE:NAME --env
BB_TARGET_FPS=...` runs the original with the patch's bytes applied against our replacement with
the option, on the edge cases and on frames recorded in the game at 60 FPS.

An earlier version only changed the limiter. Because the hooked frame step no longer ran the
patches' delta-time code, at 60 FPS the game logic would still have been told every frame lasts
1/30 s; checking against the patches found that, and that the limiter's own settings differed.

The other functions the patches edit are not replaced yet, so for 60 FPS the community patch is
still needed for them (`BB_FPS=60` in the scaffold); its edits inside our three functions do not
run, and our option does their part. The frame rate has not been measured on a real display yet.

A second option, `BB_LIMITER_WAIT=sleep`, makes the limiter use the sleeping wait the game
carries but never selects: it sleeps until about 5 ms before the frame's end and spins the
rest, so a fast machine does not keep a core busy waiting.

## Open questions

- What `+0x14` and the counters at `+0x268` / `+0x26c` mean (the latter set how many recent
  late frames the catch-up sums).
- Which subsystem the `0x0111a...` clock layer belongs to.
- What each live function the 60 FPS patches edit belongs to, and which run in gameplay.
