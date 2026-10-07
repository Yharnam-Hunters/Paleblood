# runtime

Generic PS4 layer: hook registry mechanism now; later memory map, threading,
module loading, and the OS and library interfaces the game needs. Nothing here
may name the game, a tracked function, or an address (`tools/check_agnostic.py`
enforces it). If a feature needs game knowledge, the mechanism lives here and the
knowledge lives in `game/` as data.
