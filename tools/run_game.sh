#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run the game on the scaffold for a fixed time, holding the GPU lock.
#
#   tools/run_game.sh SECONDS [hook]       (environment below)
#
# - Takes an exclusive flock on $BB_GPU_LOCK (default $BB_DATA_ROOT/gpu.lock) for the whole run,
#   waiting up to $BB_GPU_LOCK_WAIT seconds (default 1800); other projects on this machine take
#   the same lock. Refuses to start when the GPU is busy anyway.
# - With `hook`, loads $BB_GAME_LIB (default build/game/libbbgame.so) and records inputs into
#   the capture library $BB_CAPTURE_DIR (default ../captures next to the repo).
# - $BB_DISPLAY=:NN runs on that Xvfb display (started if needed, stopped after) and saves a
#   frame every 15 s to $BB_DATA_DIR/frames/<run>/; unset uses the current display.
# - Needs: $BB_SCAFFOLD (the bbport checkout with a build), $BB_GAME_DIR (merged dump),
#   $BB_DATA_DIR (writable, outside the repository). $BB_FPS (default 30: no frame-rate patches).
# - $BB_ROUTE: a pad route (timed presses, waits on on-screen text; tools/play_route.py), so
#   unattended runs get past the menus. Its log is <run log>.route.
# - $BB_RECORD=START:SECONDS (needs $BB_DISPLAY): records the display from START for SECONDS
#   into frames/<run>/clip.mp4 (960x540, 15 FPS), for documentation.
# Prints the run's log path and a short result; exits 0 when the game ran the whole time.
set -uo pipefail
secs=${1:?usage: run_game.sh SECONDS [hook]}
mode=${2:-stock}
repo=$(cd "$(dirname "$0")/.." && pwd)
: "${BB_SCAFFOLD:?set BB_SCAFFOLD}" "${BB_GAME_DIR:?set BB_GAME_DIR}" "${BB_DATA_DIR:?set BB_DATA_DIR}"
data_root=${BB_DATA_ROOT:-$(cd "$(dirname "$0")/.." && pwd)/../data}
lock=${BB_GPU_LOCK:-$data_root/gpu.lock}
run=$(date +%Y%m%d-%H%M%S)-$mode
log=$BB_DATA_DIR/logs/$run.log
mkdir -p "$BB_DATA_DIR/logs"

exec 9>>"$lock"
if ! flock -w "${BB_GPU_LOCK_WAIT:-1800}" 9; then echo "run_game: GPU lock $lock still held; not starting"; exit 9; fi
util=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null || echo 0)
if [ "${util:-0}" -gt 20 ]; then echo "run_game: GPU at ${util}% without the lock holder; not starting"; exit 9; fi

xvfb=
if [ -n "${BB_DISPLAY:-}" ]; then
    if [ ! -S "/tmp/.X11-unix/X${BB_DISPLAY#:}" ]; then
        Xvfb "$BB_DISPLAY" -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
        xvfb=$!
        for _ in $(seq 50); do [ -S "/tmp/.X11-unix/X${BB_DISPLAY#:}" ] && break; sleep 0.1; done
    fi
    unset WAYLAND_DISPLAY XAUTHORITY
    export DISPLAY=$BB_DISPLAY SDL_VIDEODRIVER=x11
fi
export BB_FPS=${BB_FPS:-30} BB_UPSCALER=${BB_UPSCALER:-fsr3} BB_FRAME_STATS=1 BB_TIMEOUT=0
pad=$BB_DATA_DIR/pad.$$
: > "$pad"
export BB_PAD_FILE=$pad
if [ "$mode" = hook ]; then
    export BB_GAME_LIB=${BB_GAME_LIB:-$repo/build/game/libbbgame.so}
    export BB_CAPTURE_DIR=${BB_CAPTURE_DIR:-$(cd "$repo/.." && pwd)/captures} BB_CAPTURE_RUN=$run
    # BB_CAPTURE=0: hooks without recording; BB_CAPTURE_ONLY=fn,...: record only those. Recording
    # every hooked function slows frames enough to crash the runtime in the opening cutscene.
    [ "${BB_CAPTURE:-1}" = 0 ] && unset BB_CAPTURE_DIR BB_CAPTURE_RUN
fi

(cd "$BB_SCAFFOLD" && exec bash run.sh) >"$log" 2>&1 &
pid=$!
frames=$BB_DATA_DIR/frames/$run
record_pid=
if [ -n "${BB_RECORD:-}" ] && [ -n "${BB_DISPLAY:-}" ]; then
    (
        sleep "${BB_RECORD%%:*}"
        mkdir -p "$frames"
        exec nice -n 10 ffmpeg -loglevel error -y -f x11grab -framerate 15 -video_size 1920x1080 -i "$BB_DISPLAY" \
            -t "${BB_RECORD##*:}" -vf scale=960:540 -c:v libx264 -preset veryfast -crf 28 -pix_fmt yuv420p \
            -movflags +faststart "$frames/clip.mp4"
    ) &
    record_pid=$!
fi
route_pid=
if [ -n "${BB_ROUTE:-}" ]; then
    python3 "$(dirname "$0")/play_route.py" "$BB_ROUTE" "$pad" "$frames" "${BB_DISPLAY:-}" >"$log.route" 2>&1 &
    route_pid=$!
fi
status=ran
for ((t = 5; t <= secs; t += 5)); do
    sleep 5
    if ! kill -0 "$pid" 2>/dev/null; then status="exited at ${t}s"; break; fi
    if [ -n "${BB_DISPLAY:-}" ] && [ $((t % 15)) -eq 0 ]; then
        mkdir -p "$frames"; import -window root "$frames/t$(printf %03d "$t").png" 2>/dev/null
    fi
done
cpu=$(awk -v hz="$(getconf CLK_TCK)" '{printf "%.0f", ($14 + $15) / hz}' "/proc/$pid/stat" 2>/dev/null || echo "?")
if kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid"
    for _ in 1 2 3 4 5 6; do sleep 1; kill -0 "$pid" 2>/dev/null || break; done
    kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
fi
wait "$pid" 2>/dev/null
[ -n "$route_pid" ] && kill "$route_pid" 2>/dev/null
[ -n "$record_pid" ] && wait "$record_pid" 2>/dev/null
rm -f "$pad"
[ -n "$xvfb" ] && kill "$xvfb" 2>/dev/null
echo "run_game: $run ($status, game CPU time ${cpu} s over ${secs} s) log $log"
grep -E "Game library|STOP|Guest fault" "$log" | head -5
grep "^Frame stats" "$log" | tail -1 | cut -c1-120
[ "$status" = ran ]
