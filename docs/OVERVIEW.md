# Overview

The technical details behind the [README](../README.md): what Paleblood is made of, what works,
how it works, how to run the game, and the exact target. The full progress numbers are in
[STATUS.md](../STATUS.md); the [documentation site](https://yharnam-hunters.github.io/byrgenwerth-site/)
tells the story in depth.

## What this is

Paleblood is two things built side by side:

- **A decompilation.** The game's 1.09 PS4 executable is rewritten as readable C++, function by
  function. It is a *functional* decompilation: a replacement does not have to compile to the same
  bytes, it has to **behave** exactly like the original, and each one is proved to, against the
  original itself. Some functions also match the original byte for byte
  ([MATCHING.md](MATCHING.md)).
- **A native runtime.** The game's x86-64 code runs directly on your PC. Paleblood supplies the
  PS4 system libraries it calls (threads, memory, files, sound, video out, the GPU), written from
  public facts. There is no emulator: the GPU is rebuilt at the engine's own level, translating the
  game's shaders and replaying its own draw calls on Vulkan (Metal on macOS in progress).

The goal is a complete source port: every function rewritten and proved, the game running natively
on Paleblood alone, and a game that can be read, preserved, fixed and modded like any other
program. You always bring your own copy of the game.

## What works

- **Boot and menus.** In the maintainer's local runs the game plays its logos and title screen,
  takes "Play Offline", creates its save data, shows the brightness and controls screens, plays its
  opening cinematic in 3D, goes through character creation and loads the first area, Iosefka's
  Clinic, drawn in 3D. The boot record in STATUS.md is the plain run (no display, no input,
  30 seconds), so its milestone stays at the first flip.
- **System libraries.** Every system function the executable imports is answered, either by the
  game's own bundled modules or by Paleblood's runtime, from public facts. Online features answer
  as a console that is signed out and offline.
- **Renderer.** The game's own GCN shaders are translated to SPIR-V and its draws and compute
  passes replayed on Vulkan, in the PS4's submission order on a thread of its own. Render targets
  and images are tiled and detiled on the GPU ([RENDERER.md](RENDERER.md)). A Metal executor for
  macOS builds and passes its tests in CI; a full game boot or a window on macOS is not
  established yet ([macos-metal.md](macos-metal.md)).
- **Decompilation.** The frame loop and functions across the event, kernel, input, loading, AI,
  character, physics and render layers are replaced and proved. Function families, many tiny
  identical functions (an empty method, a getter), share one replacement each
  ([FAMILIES.md](FAMILIES.md)): they move the function count much more than the byte count, so
  most of the work is still ahead. `BB_TARGET_FPS=30|60|uncapped` already does in code what the
  community frame-rate patches do with byte edits.
- **Source files.** `tools/source_files.py` tags each function with the library (the engine,
  Havok, FMOD, YEBIS, ...) and the source file it probably came from (`symbols/source_files.csv`).
- **The external scaffold** (`third_party/bbport/`) is still kept for full game runs, until
  Paleblood's own runtime passes the same run checks.

Contributors are credited on the documentation site's
[Hunters page](https://yharnam-hunters.github.io/byrgenwerth-site/hunters/); what happens next is
in [STATUS.md](../STATUS.md) and [NEXT.md](../NEXT.md), and the
[runtime page](https://yharnam-hunters.github.io/byrgenwerth-site/runtime/) has the full history.

## How it works

**Decompilation track**

1. **Map the original.** Our loader ([`runtime/loader.c`](../runtime/loader.c)) maps the
   unmodified executable from your own dump: segments, relocations, and every system import bound.
2. **Rewrite a function.** A replacement in `game/<system>/`, readable ([STYLE.md](../STYLE.md))
   and faithful to the original; [`game/hooks.csv`](../game/hooks.csv) registers it.
3. **Prove it.** [`tools/verify.py`](VERIFY.md) runs the original machine code and the
   replacement side by side on recorded and generated cases and compares return values,
   registers, every byte written and every library call. Deliberately wrong versions must fail.
4. **Review it.** An independent reviewer checks the verification and the readability before a
   function counts as verified ([CONTRIBUTING.md](../CONTRIBUTING.md), "Definition of done").
5. **Families** ([FAMILIES.md](FAMILIES.md)): tiny functions with identical bytes share one
   replacement and are proved member by member in batches, with deliberately wrong versions that
   must fail. The progress table counts them on their own row.
6. **Where a function came from.** `symbols/source_files.csv` tags each function with its likely
   library and source file, so you know what you are reading before you start.

**Runtime track**

7. **System libraries** (`runtime/syslib/`): each PS4 call the game makes is answered from public
   facts, or stops by name. Nothing is guessed.
8. **Renderer** (`runtime/renderer/`, [RENDERER.md](RENDERER.md)): the game's GPU library calls
   are observed at the engine level, its GCN shaders translated to SPIR-V, and its draws and
   compute passes replayed on Vulkan, in submission order on the renderer's own thread. No
   command-stream emulation.
9. **Movies, sound and input:** movies decode with FFmpeg, sound (ATRAC9) with LibAtrac9, and the
   controller comes from a gamepad, the keyboard or a timed script.
10. **Boot further.** `tools/boot.py` measures how far the game gets on the runtime alone.

## Running the game

You need Linux (the primary platform, tested by CI) or macOS (in progress), and a dump of your own
copy of the game from your own PS4 ([LEGAL.md](LEGAL.md)).
[ONBOARDING.md](ONBOARDING.md) goes from your own dump to your first pull request: preparing the
dump (`tools/prepare_dump.sh`), Ghidra with the GhidraOrbis loader ([GHIDRA.md](GHIDRA.md)), the
verification harness and a first verified function.

```
git clone https://github.com/Yharnam-Hunters/Paleblood.git
cd Paleblood
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
tools/install_hooks.sh
```

Boot the game in a window, with sound and controls (Vulkan, SDL3 and FFmpeg needed):

```
python3 tools/boot.py --no-record --game-lib build/game/libbbgame.so \
    --display window --audio host --input host --timeout 600
```

Keyboard: WASD and the arrow keys are the sticks, IJKL the D-pad, Enter/Space cross, Backspace
circle, R square, F triangle, Q/E L1/R1, Z/C L2/R2, Esc options, T the touch pad; a gamepad works
by button position.

Without a person at the controller, a timed script presses the buttons instead (seconds, or `700f`
for presented frames; the format is in
[`runtime/include/runtime/input_host.h`](../runtime/include/runtime/input_host.h)):

```
python3 tools/boot.py --no-record --game-lib build/game/libbbgame.so \
    --display headless --display-capture frames/ --input-script "45:down,47:cross" --timeout 120
```

Character creation's contract needs a name: `--keyboard-text Hunter` answers the on-screen
keyboard as if that name was typed (without it the keyboard is canceled). `--savedata-from DIR`
starts the run's saves as a copy of DIR, a save folder an earlier boot kept with `--savedata`, so a
boot can continue from the game's own save instead of replaying the start.

Plain `python3 tools/boot.py` (no options) runs without the replacements, display, sound or
input, and records how far the boot gets.

## Options

| Variable | Effect |
|---|---|
| `BB_TARGET_FPS=30`, `60`, `uncapped` | Frame rate target, as the community "30 FPS++", "60 FPS++" and "Uncap FPS++" patches. Unset: the original behaviour, exactly. |
| `BB_LIMITER_WAIT=sleep` | The frame limiter sleeps instead of spinning while it waits (the game carries this wait but never selects it). |

## Layout

| Path | What |
|---|---|
| `game/<system>/` | Replacement code, one directory per game system, with its case generators. |
| `game/hooks.csv` | Hook registry: original address to replacement. |
| `runtime/` | The native runtime: loader, boot harness, system libraries (`syslib/`), renderer, display, audio, input. Nothing specific to this game. |
| `symbols/` | `functions.csv` (what we track), `ghidra_functions.csv` (every function, the denominator), boot records. |
| `tools/` | Verification harness, boot harness, progress, validators, Ghidra scripts, hooks. |
| `test/` | Unit and GPU tests; system library tests in `test/syslib/`. |
| `third_party/` | LibAtrac9 (MIT, unmodified) and the external run scaffold, each with its license (`third_party/README.md`). |
| `docs/` | Onboarding, Ghidra setup, architecture, renderer, verification, system pages. |

## Target

CUSA03173 (EU), update v1.09 merged over the base game. Hashes of the executables converted
from SELF to ELF (`target.sha256`); addresses in `symbols/` are PS4 virtual addresses (Ghidra
image base `0x400000`, the same as the community patches). See [QUIRKS.md](../QUIRKS.md).

<!-- target:start -->
```
ca8813862d7b1345a6791b7d88cc1d383ede2a6bbc944f39dd3c0e0e3ad9e285  eboot.elf
6552d61af5258817f434c00f759e194c5d97ac97cfc7ce11b1f23aec11f94d0a  libc.elf
8def93e0cd3224619d3d51ddfda8b3dd745e6707d1398fd399d5f04199a7caab  libSceFace.elf
16369566417f7bacd34ebd75f8cffb586563da3e2c40fe8880c60d76718e4833  libSceFios2.elf
6ec2b7bd0cb1dcfe491f3981301c6f30836e34d06e97a1aaaa609580dc2a899a  libSceHand.elf
3f303ae1bd114380729987698b5aa4cfcfeb11d2dc6c9d09f5d6ae9baa53ce18  libSceHeadTracker.elf
b653f26abcf0fd185b4110a6aacc123f7e2e8c0d085f8e5ba1698123213b6659  libSceS3DConversion.elf
a85c286a110f406682dafc97819318eedc9aa1591b79465f1ef9c1929655de8c  libSceSmart.elf
```
<!-- target:end -->
