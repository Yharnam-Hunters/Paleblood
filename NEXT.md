# Next targets

The queue the per-function pipeline works through. One line per
target, in order; move a target to "Done" with its commit when it is verified. Decisions and
blockers go to STATUS.md instead of being worked around.

## Queue

1. The live functions the
   60 FPS patches edit besides frame timing (47 live, 12 dead:
   `tools/patch_overlap.py ... --map`, `tools/reloc_refs.py`, `tools/refs_to.sh`). Done so far:
   the nine fixed-step state methods (Chalice Dungeon state machine; replaced, verified on edge
   cases stock and against "60 FPS++", waiting for a save that reaches the Chalice Dungeons).
   Still to do among those called in the clinic (probe run 2026-10-06, `continue.route`):
   `0x0183ac60` (20 KB; every frame-rate patch changes a jump and a flag) and `0x02713870`
   (11.6 KB; its only edit, Uncap FPS++'s, is broken in v1.09: QUIRKS.md). Each option part is
   checked against the patch that edits it (both sides patched, VERIFY.md).

## Done

- `0x0138a370` frame_timing_task_dispatch_0138a370: verified (the task dispatcher; 51 recorded frames with 76 registered tasks each). With it the task manager's frame is done: `0x024512a0`, `0x01388c60`, `0x01388c70`, `0x0143e490`, `0x014400a0`, `0x0143f9f0`.
- `0x0143e490` frame_timing_task_flush_queue_0143e490 and `0x014400a0` frame_timing_task_workers_step_014400a0: verified (200 and 53 recorded calls; the list re-read after each task cannot be made observable with stubs).
- `0x024512a0` frame_timing_task_frame_024512a0 and `0x01388c70` frame_timing_task_run_01388c70: verified (51 recorded calls each; the singleton re-read after the fatal error cannot be told apart in the harness, whose stubs cannot set a global).
- `0x01388c60` frame_timing_task_run_all_01388c60 and `0x0143f9f0` frame_timing_task_set_frame_value_0143f9f0: verified (task manager's frame; 51 recorded calls each).
- `0x0111a7f0` frame_timing_get_monotonic_ms: verified (1a7c39e).
- `0x02434770` frame_timing_pace_frame: faithful replacement verified; `BB_TARGET_FPS` and `BB_LIMITER_WAIT` options.
- `0x02418d20` frame_timing_frame_step: verified (first function with virtual calls).
- `0x02434520` frame_timing_flipper_init: verified.
- `0x013d3520` frame_timing_task_013d3520: verified (stock and against Uncap FPS++).
- `0x01c0c2b0` physics_instance_construct_01c0c2b0: verified (physics instance constructor; stock and 30/60/Uncap FPS++, 19 constructions recorded while the clinic loads; the motion rescale, listeners and root update run only outside the physics manager's special modes and are covered by edge cases).
- `0x0222bc10` ai_hk_frame_update_0222bc10: verified (Havok AI manager; stock and 30/60/Uncap FPS++, 51 recorded frames; the debug display, axis frames, boxes and the manager-wide draw do not run in the clinic and are covered by edge cases).
- `0x01cbdb20` character_frame_update_01cbdb20: verified (stock and 30/60/Uncap FPS++, 57 recorded calls; the name-string block does not run in the clinic and is covered by edge cases).
- `0x025b2fb0` render_swap_chain_present: verified (GXSwapChainCtrl; stock and 30/60/Uncap FPS++, 51 recorded frames; in the clinic it presents with interval 1 without waiting).
- `0x00fbc3e0` render_yebis_get_recursive_sample_parameters: verified (YEBIS middleware; stock and 30/60/Uncap FPS++, 54 recorded calls).
- `0x02377bd0` render_frame_update_02377bd0: verified (stock and 30/60/Uncap FPS++, 51 recorded frames).
- `0x01972900` input_pad_step_01972900: verified (stock and 30/60/Uncap FPS++; its advance branch was idle in the clinic, covered by edge cases).
- `0x016efd00` event_emk_add: verified (stock and 60 FPS++; 50 calls recorded in gameplay).
- `0x02483e80` kernel_condition_wait: verified; all waits in gameplay are untimed, so the FPS
  patches' `ret` on the timed path never runs.
- `BB_TARGET_FPS` in the limiter, frame step and flipper constructor checked against the
  community patches (`verify.py run --patch`).
- `0x004632d0` (was frame_timing_get_time_us): dead in the stock game, used by the FPS patches as a
  code cave; hook and replacement removed, back to `original` with a note.
