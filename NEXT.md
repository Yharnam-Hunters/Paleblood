# Next targets

Two tracks. Sessions alternate between them (decompilation, then runtime, then decompilation,
...) unless the maintainer says otherwise; `Last session` below records which one ran last.
One line per target, in order; move a target to "Done" with its commit when it is finished.
Decisions and blockers go to STATUS.md instead of being worked around.

Last session: runtime

## Runtime track

How far the executable boots on our own runtime (`runtime/boot.c`, `tools/boot.py`): the boot
stops at the first system import we don't implement yet and names it. Each target implements
what the boot stops at, clean room (CONTRIBUTING.md), then re-runs `tools/boot.py`.

1. `scePthreadAttrGetaffinity` (libkernel), called by the game's own libc while it starts: needs
   the main thread's CPU affinity mask on a PS4 (STATUS.md, "Open questions") and a way to replace
   a function inside a bundled module (claimed by BluePinetree on #34). Merged `main` stops here.
   Past it (local discovery boots), the boot reaches milestone `audio` and stops at:
2. `sceGnmValidateCommandBuffers`: the engine reads exactly `0x80d13fff` as "validation
   unavailable" (`0x02abb950`); that code has no public name yet. The next GNM call after it,
   `sceGnmDispatchInitDefaultHardwareState`, writes the GPU's own packets into the engine's
   command buffer, so GNM is not answerable call by call: the engine builds its command buffers
   itself and only submits them (map of all 50 imports on #39).
3. The engine's renderer (its `GX` graphics framework), above where its command buffers are
   built: the per-frame routine `0x025d5f80` (it submits at `0x025d861b`), the submit-and-flip
   helpers `0x02ab0ce0` and `0x02ab0e30`, and the other submit clusters listed on #39. This is
   where the graphics get rebuilt.

Roadmap after the boot gets going: kernel, threads and memory; files; input; audio; video
out; GNM and shaders (the largest). Claimable work per system call group is in the issues
labelled `runtime`.

## Decompilation track

Readability burn-down first (STYLE.md; CONTRIBUTING.md, "Definition of done"): refactor each
file in `tools/readable_allowlist.txt`, re-verify every recorded and edge case, get the
independent review, remove its line. Order: `game/ai/hk_ai_frame_update.cpp`, then the others
from the smallest up. New functions go to `main` only verified and readable.

1. The live functions the 60 FPS patches edit besides frame timing (47 live, 12 dead:
   `tools/patch_overlap.py ... --map`, `tools/reloc_refs.py`, `tools/refs_to.sh`). Done so far:
   the nine fixed-step state methods (Chalice Dungeon state machine; replaced, verified on edge
   cases stock and against "60 FPS++", waiting for a save that reaches the Chalice Dungeons), and
   five methods of SprjDungeonGateIns, which owns that state (two steps of its step machine and
   three event handlers; edge-verified the same way). The owner's own small methods that 60 FPS++
   edits (`0x01ea3fc0`, `0x01ea4040`, `0x01ea40e0`, `0x01ea54d0`, `0x01ea62e0`) are copies nothing
   calls or points to (dead). Larger live ones near them (each called directly): `0x01ea47a0`,
   `0x01ea4a10`, `0x01ea4cb0`, `0x01ea5280`, `0x01ea5670`, `0x01ea5850`, `0x01ea5e20`,
   `0x01ea6080`, `0x01ec3ef0`.
   Still to do among those called in the clinic (probe run 2026-10-06, `continue.route`):
   `0x0183ac60` (20 KB; every frame-rate patch changes a jump and a flag) and `0x02713870`
   (11.6 KB; its only edit, Uncap FPS++'s, is broken in v1.09: QUIRKS.md). Each option part is
   checked against the patch that edits it (both sides patched, VERIFY.md).

## Done

- Runtime, 2026-10-30/31 (#143-#180): direct and flexible memory with protection and virtual
  queries; semaphores, condition variables (recursive waits), thread creation, priorities,
  attributes and signal masks; thread handles below 2^40; the time of day, FreeBSD clocks and
  process time; the console's start-up services (user, common dialog, language, app content,
  content restriction); the network, SSL and HTTP libraries' offline start-up; audio (AJM start,
  output ports, volume, real-time pacing, port state); /app0 and read-only file access; kernel
  event queues; video out ports, buffers and hsync flips; the first GNM calls (tessellation
  factor ring, compute queues). Generic input capture landed in #133-#135.

- `0x01f03210`, `0x01f03680`, `0x0200f970`, `0x020118f0`: state methods that find the current
  Chalice Dungeon gate through SprjHolygrail (event flags 9020..9026 pick it), advance it while
  busy and report on its mode: edge-verified (stock and against "60 FPS++").
- `0x01eb5390`, `0x01eb5490`, `0x01eb9020`, `0x01ebb090`, `0x01ebf060`: SprjDungeonGateIns
  STEP_Init_forChannelLoad, STEP_ModeSwitch and the handlers of events 0, 1000 and 3000:
  edge-verified (stock and against "60 FPS++"); waiting for a save that reaches the Chalice Dungeons.
- Runtime: libkernel `sceKernelGetProcParam`, `_sceKernelRtldSetApplicationHeapAPI`, the mutex and mutex attribute functions (SCE and POSIX names), `scePthreadSelf` and thread attributes (init, destroy, get).
- Runtime: the game's own libc and libSceFios2 modules are mapped as guest code (`game/modules.csv`) and the executable's imports they export are bound to them; their functions are tracked in `symbols/module_functions.csv`, replaceable later like game functions.
- Runtime: our own loader (`runtime/loader.c`) and boot harness (`runtime/boot.c`, `tools/boot.py`); the verification harness maps the executable with the loader.
- Runtime: module loading at run time (libSceSysmodule `sceSysmoduleLoadModule`,
  `sceSysmoduleIsLoaded`, `sceSysmoduleUnloadModule`): a module `game/modules.csv` lists with action
  `stub`, now with its system module id, is reported loaded without being mapped; any other module,
  a second load and the "not loaded" answers stop by name.

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
