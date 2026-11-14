# Enemy debug overlay (design)

A renderer mode that draws, on top of the game's own frame, what an enemy's AI is doing: its hit
shapes, its detection cones and radii, its current AI state, and the line to its target. It turns
decompiled AI structures into pictures, so the documentation can show how an enemy works instead
of describing it.

**Status: design and hooks only.** Nothing here is built yet. The drawing layer (phase 1 below)
can be built and tested now with synthetic data; the per-enemy readers wait until the enemy AI
structures are decompiled and verified. The first target is the beast in Iosefka's Clinic, then
the enemies of Central Yharnam, in the order the game introduces them.

Clips of a run, with or without the overlay, are recorded with `tools/boot.py --clip`
([Recording clips](#recording-clips)).

## Rules

- **Read only.** The overlay reads game memory and never writes it. With the overlay off, a run is
  byte-for-byte the run it would have been without this feature; with it on, only the presented
  image differs (the overlay is drawn after the game's last draw into the frame).
- **Off by default.** It is enabled per run (`pbboot --debug-overlay LAYERS`), never in a recorded
  boot (`tools/boot.py` refuses `--debug-overlay` without `--no-record`, as for every option that
  changes what a run shows or does).
- **Only decompiled fields.** Every value the overlay draws comes from a structure field whose
  meaning is established by decompiled, verified code. A field whose meaning is only inferred is
  drawn with its inferred label (for example a numeric state id instead of a state name) until a
  verified function names it. Nothing is drawn from guesses or genre convention.
- **Game knowledge stays under `game/`.** The drawing layer in `runtime/` is game-agnostic (it
  draws lines, shapes and text it is given; `tools/check_agnostic.py` applies). Everything that
  knows an enemy, a structure offset or an address lives in `game/ai/overlay/`.
- **Our own drawing.** Lines, shapes and a small built-in bitmap font are drawn by Paleblood's
  renderer. No game font, texture, model or shader is used for the overlay.
- **Local output.** Frames and clips of a run hold the game's own image: they stay on your machine
  unless the maintainer reviews one for the documentation (the wiki's media rule: silent, full
  resolution, at most 8 MiB per clip, one review per clip).

## What it draws

Each layer can be turned on by name (`--debug-overlay hitboxes,perception,state,target,leash`, or
`all`).

| Layer | Drawn | Read from (once decompiled) |
|---|---|---|
| `hitboxes` | The attack's hit shapes (spheres, capsules) while they exist, coloured by attack phase: wind-up, active, recovery | The attack's hit-shape records and the frame they switch on and off |
| `hurtboxes` | The body's damage shapes, thin outline | The character's collision shapes ([Physics](systems/physics.md), [Character](systems/character.md)) |
| `perception` | Sight cone, hearing radius, any other sense, flat on the ground at the enemy's feet; filled when the player is inside | The enemy's perception parameters and the test that uses them |
| `state` | A label over the enemy: state name (or id), time in state, current attack and its frame | The AI's current state and timers |
| `target` | A line from the enemy to its current target, dashed while the target is only remembered | The AI's target reference and its "last known position" |
| `leash` | The home point and the distance at which the enemy gives up | The return/chase-end test and its inputs |
| `frame` | Frame number and game time in a corner, for matching overlay clips to timelines | The renderer's presented-frame counter |

The phase colours match the wiki's attack timeline (wind-up gold, active red, recovery blue,
tracking violet, punish window green), so an overlay clip and its timeline read as one picture.

## How it fits together

```
game frame ─► AI update (game library replacement) ─► overlay collector (game/ai/overlay/, read only)
                                                          │  primitives: shapes, lines, text,
                                                          ▼  in world space
                                     runtime/debug_overlay (agnostic) ─► renderer overlay pass ─► flip
                                                          │
                                                          └► --overlay-log FILE (one JSON line per frame)
```

1. **Collector (`game/ai/overlay/`).** Called once per frame after the AI update, only when the
   overlay is on. It walks the live enemies and, for each, emits primitives through the agnostic
   API below. Today the walk can only reach as far as the verified AI manager update:
   `ai_hk_frame_update_0222bc10` steps the AI world and owns the groups → elements → objects lists
   ([AI](systems/ai.md), "Structs and globals": group count `+0x2bc8`, groups `+0x2bd0`, element
   object list `+0x60`). Which objects are enemies, and where each keeps its state, is the first
   thing the enemy AI decompilation has to establish.
2. **Primitive API (`runtime/include/runtime/debug_overlay.h`, planned).** Game-agnostic calls,
   collected per frame into a buffer the renderer owns:

   ```c
   void rt_overlay_begin_frame(uint64_t frame);
   void rt_overlay_set_view(const float view_projection[16]);   /* world to clip space */
   void rt_overlay_line(const float a[3], const float b[3], uint32_t rgba, unsigned flags);
   void rt_overlay_sphere(const float centre[3], float radius, uint32_t rgba, unsigned flags);
   void rt_overlay_capsule(const float a[3], const float b[3], float radius, uint32_t rgba, unsigned flags);
   void rt_overlay_ground_cone(const float apex[3], const float forward[3], float radius,
                               float angle_degrees, uint32_t rgba, unsigned flags);
   void rt_overlay_ground_circle(const float centre[3], float radius, uint32_t rgba, unsigned flags);
   void rt_overlay_text(const float anchor[3], const char *utf8, uint32_t rgba);  /* ASCII drawn */
   ```

   `flags`: filled or outline, dashed, depth-tested or always on top. A call made while the overlay
   is off returns at once.
3. **Overlay pass (renderer).** After the game's last draw into the frame being presented and
   before the flip: lines and shape outlines as line lists, fills as triangles with alpha, text as
   textured quads from the built-in font. One small shader pair per backend, written for Vulkan
   and Metal like the rest of the renderer ([RENDERER.md](RENDERER.md)). Depth testing against the
   game's depth target is optional per primitive; the default draws through walls, which is what a
   reader needs to see a cone behind a pillar.
4. **Camera.** World-space primitives need the frame's view and projection. They come from the
   camera the game renders with ([Camera](systems/camera.md); `camera_update_follow` is verified)
   through a game-side hook that hands the matrix to `rt_overlay_set_view`. Until that matrix is
   located, the collector can still emit screen-space text.
5. **Overlay log.** `--overlay-log FILE` writes one JSON line per frame with what the collector read
   (enemy, state, attack, attack frame, distances). It is the bridge to the wiki's diagrams: a
   timeline measured from a run is labelled *observed*; the same timeline read from verified code
   is labelled *verified*. Like frames, the log stays local unless reviewed.

### A second source: the game's own AI debug display

The verified AI update already contains the game's own AI debug display and drawing, gated by flags
in the manager (`+0x2c90`..`+0x2c98`), with a box mode at `+0x2c68` and per-object drawing. In the
Iosefka's Clinic recordings the display, axis frames and boxes stay off. Turning them on in a local
run (a game-side debug option, never in a recorded boot) would show what the original developers
chose to draw, if the retail build still routes those primitives to the renderer. That is a
cross-check for the overlay, not a replacement: it covers the Havok AI world, which may not be
where combat decisions live, and its primitives would first have to reach Paleblood's renderer.

## Hooks still to find

Each needs decompiled, verified code before the overlay can use it.

| Hook | Needed for | Evidence that would establish it |
|---|---|---|
| Live enemy list | Every layer | The code that registers and iterates enemies each frame |
| AI state and timers | `state` | The update that changes an enemy's state, with its transitions |
| Perception parameters and test | `perception` | The function that tests the player against an enemy's senses |
| Target reference | `target` | The code that picks and forgets a target |
| Attack hit shapes and timing | `hitboxes` | The code that switches an attack's hit shapes on and off by frame |
| Home point and give-up test | `leash` | The chase-end condition and its inputs |
| View-projection matrix | All world-space drawing | Where the frame's camera matrices are built or handed to the renderer |

## Tests

- **Drawing layer (now, synthetic).** Project known primitives with known matrices and compare the
  pixels of a captured frame (`--display-capture`) with the expected positions; text renders the
  built-in font's glyphs; an overlay that is off leaves every frame bit-identical.
- **Collectors (later).** Each collector runs against synthetic structures built in its test
  (`game/ai/overlay/*_cases.py`, like other cases) and emits the expected primitives; it never
  writes the structures it reads.
- **End to end (local).** A run to Iosefka's Clinic with `--debug-overlay all --clip` records an
  overlay clip; the overlay log's frames line up with the clip's frame numbers.

## Recording clips

`tools/boot.py --no-record --display headless --clip FILE --clip-start N --clip-frames COUNT`
records presented frames N to N+COUNT-1 of a run to a silent clip at full resolution:

- `FILE` ends in `.webm` (VP9) or `.mp4` (H.264) and must be outside the repository;
- frames play at `--clip-fps` (default 30, the game's presentation rate), one presented frame per
  step, however fast the run itself went;
- the clip has no audio track (`-an`) and is never scaled (an odd width or height gets one row or
  column of padding, which the encoders need);
- without `--display-capture`, the frames go to a scratch folder next to `FILE` that keeps only the
  frames in range while the run goes, so a long run does not fill the disk;
- a clip over 8 MiB is written with a warning: the wiki's cap for one clip.

Use it with `--input-script` (and `--savedata-from` for a saved checkpoint) to reach the same moment
in every run, so a behaviour clip and its overlay clip show the same encounter.

## Phases

1. **Drawing layer**: the primitive API, the overlay pass for Vulkan and Metal, the built-in font,
   `--debug-overlay` and `frame` (agnostic; synthetic tests).
2. **Camera matrix**: the view-projection hook, so world-space shapes land in place.
3. **First enemy**: collectors for the Iosefka's Clinic beast as its AI is decompiled and verified,
   layer by layer; the overlay log.
4. **Documentation**: overlay clips and run-measured timelines for its page, each clip reviewed on
   its own.
