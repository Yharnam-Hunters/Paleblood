# Next targets

The queue the per-function pipeline works through without asking (CLAUDE.md). One line per
target, in order; move a target to "Done" with its commit when it is verified. Decisions and
blockers go to STATUS.md instead of being worked around.

## Queue

1. The live functions the 60 FPS patches edit besides frame timing (47 live, 12 dead:
   `tools/patch_overlap.py ... --map`, `tools/reloc_refs.py`, `tools/refs_to.sh`). Done so far:
   the nine fixed-step state methods (Chalice Dungeon state machine; replaced, verified on edge
   cases stock and against "60 FPS++", waiting for a save that reaches the Chalice Dungeons).
   Called in the clinic (probe run 2026-10-06, `continue.route`): `0x02377bd0` (399 B, once a frame,
   Uncap), `0x016efd00` (368 B), `0x00fbc3e0` (670 B),
   `0x025b2fb0` (768 B, once a frame, Uncap), `0x01cbdb20` (979 B), `0x0222bc10`,
   `0x0183ac60`, `0x02713870`, `0x01c0c2b0`. Work them smallest first; each option part is
   checked against the patch that edits it.
2. `0x024512a0` task update (the game's frame of work): analyze, split into targets.

## Done

- `0x0111a7f0` frame_timing_get_monotonic_ms: verified (1a7c39e).
- `0x02434770` frame_timing_pace_frame: faithful replacement verified; `BB_TARGET_FPS` and `BB_LIMITER_WAIT` options.
- `0x02418d20` frame_timing_frame_step: verified (first function with virtual calls).
- `0x02434520` frame_timing_flipper_init: verified.
- `0x013d3520` frame_timing_task_013d3520: verified (stock and against Uncap FPS++).
- `0x02377bd0` render_frame_update_02377bd0: verified (stock and 30/60/Uncap FPS++, 51 recorded frames).
- `0x01972900` input_pad_step_01972900: verified (stock and 30/60/Uncap FPS++; its advance branch was idle in the clinic, covered by edge cases).
- `0x016efd00` event_emk_add: verified (stock and 60 FPS++; 50 calls recorded in gameplay).
- `0x02483e80` kernel_condition_wait: verified; all waits in gameplay are untimed, so the FPS
  patches' `ret` on the timed path never runs.
- `BB_TARGET_FPS` in the limiter, frame step and flipper constructor checked against the
  community patches (`verify.py run --patch`).
- `0x004632d0` (was frame_timing_get_time_us): dead in the stock game, used by the FPS patches as a
  code cave; hook and replacement removed, back to `original` with a note.
