# The renderer

How Paleblood puts Bloodborne's frames on screen: Vulkan on Linux, Metal on macOS. This is the
plan the maintainer chose on 4 November 2026 (issue #39), and where each part stands.

## The approach

Bloodborne draws through its own engine renderer (the `GX` framework) and Sony's GPU helper
library compiled into the game (`GfxContext`). Together they turn each draw into GPU command
buffers, which the game then hands to the system. Its shaders ship as compiled AMD GPU code
(GCN, the PS4 GPU's instruction set) inside its `.shaderbnd` bundles.

Paleblood does not emulate the PS4's GPU, and it does not replay the finished command buffers.
Instead:

- **Engine draw hooks.** The game's own draw layer is replaced function by function, the same way
  as the rest of the decompilation. Render targets, clears, shader binding, buffers, textures,
  state and draws go to Paleblood's renderer instead of into command buffers.
- **Shader translation.** Each shader the game loads is translated from GCN code to SPIR-V for
  Vulkan, and to MSL for Metal. The translator is written clean-room from AMD's public
  instruction-set manuals; no emulator's translator is consulted or modelled. Translation runs on
  your machine from your own copy of the game, and nothing translated is committed or published.
- **Until the hooks draw,** GPU work goes through the temporary stand-in in
  `runtime/syslib/gnm.c`. It completes each submission at once so start-up can continue, and it
  draws nothing.

## Phases

1. **Display** (this phase). Every frame buffer the game registers with video out has a host
   frame, and each flip shows it (`runtime/display/`, `runtime/display.h`).
   - `pbboot --display window` opens a window, with SDL3 and Vulkan.
   - `--display headless` keeps and counts the frames.
   - `--display-capture DIR` writes every presented frame as a PPM image, for tests and for
     looking at a run.
   - `tools/boot.py` passes both options on. The default, `none`, leaves boots as they were.
   - Today the frames are black: nothing draws into them yet. macOS gets a Metal window next
     (building on the offscreen Metal unit of #213).
2. **Shader translator.**
   - A decoder for the GCN instruction set, tested on encodings built from the manual.
   - A local census tool that decodes the shaders in your own copy and reports which
     instructions they use, so the translator covers what the game needs first.
   - SPIR-V output for that set, then MSL.
3. **Engine draw hooks.** Map the draw layer: the shader-binding helpers at
   `0x01475400`–`0x01475a70` and their callers, the render-target, clear, viewport, blend and draw
   entry points. Then route them to the renderer. The first target is the title screen's 2D
   passes: clears, textured quads and text.
4. **Textures and 3D.** The game's texture formats and tiling (from public format documentation),
   meshes, materials and post-processing.

Each phase lands in small PRs with tests. A boot is recorded after each runtime change. The wiki's
Runtime page and Gallery show where it stands.
