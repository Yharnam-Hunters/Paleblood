# third_party

## bbport (submodule)

`third_party/bbport` is our private fork of bbport
(<https://github.com/deadinside28/bloodborne_pc>, GPL-2.0-or-later, contains shadPS4 code):
`Yharnam-Hunters/bbport`, pinned to a commit. It is the runtime scaffold: it loads the original
executable natively, implements the PS4 libraries the game calls and renders through a
shadPS4-derived Vulkan backend. Its `FORK.md` lists our changes to it; so far the game
library plug-in point and function hooks (`BB_GAME_LIB`, `src/bbgame.h`).

```
git submodule update --init third_party/bbport
cd third_party/bbport && bash build.sh            # dependencies: its README and shell.nix
python3 scripts/prepare.py /path/to/merged-dump --out /path/outside/repo/out
BB_GAME_LIB=/path/to/build/game/libbbgame.so BB_GAME_DIR=/path/to/merged-dump \
    BB_DATA_DIR=/path/outside/repo bash run.sh
```

CI does not fetch the submodule: this repository builds and tests without it.

### Running without a monitor

With the monitors off (or the window hidden), the desktop compositor throttles the game's
presents to about one per second, and the game runs at 1 FPS. Use a virtual display:

```
Xvfb :98 -screen 0 1920x1080x24 -nolisten tcp &
env -u WAYLAND_DISPLAY -u XAUTHORITY DISPLAY=:98 SDL_VIDEODRIVER=x11 bash run.sh
DISPLAY=:98 import -window root frame.png            # a screenshot, ImageMagick
```

Xvfb presents through CPU copies, which caps the frame rate (about 15 FPS on the title screen
with an RTX 3070); it is for tests and captures, not for playing. Check that Vulkan presents
there first: `DISPLAY=:98 vkcube --wsi xcb` (vkcube picks Wayland on its own otherwise).

Rules for anything added here: a submodule or a vendored copy with its license and headers
intact, listed here with its version, license and every local patch. Third-party code never
contains game data. Anything linked into the same process stays GPL-2.0-or-later compatible.
