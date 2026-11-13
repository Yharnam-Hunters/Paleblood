#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Frame rate of a run from its frame log (pbboot/boot.py --frame-log; runtime/display.h).

Each line of the log is one presented frame: the video out port, the frame's number on that port
and the host's monotonic clock in nanoseconds when it was presented. The figures are over the frames
of one port (the game presents on port 1) between --from-frame and --to-frame:

  average fps   frames presented after the first, divided by the time from the first to the last;
  1% low fps    the frame rate over the slowest 1% of frame intervals (at least one): their count
                divided by their summed time, the usual definition;
  worst frame   the longest single interval, in milliseconds.

Usage: tools/frame_stats.py LOG [--port N] [--from-frame N] [--to-frame N] [--json]
"""
import argparse
import json
import math
import sys

NANOSECONDS = 1e9


def read_log(path, port):
    """The (frame, nanoseconds) pairs of one port, in the order the log has them."""
    frames = []
    with open(path) as f:
        for number, line in enumerate(f, 1):
            fields = line.split()
            if not fields:
                continue
            if len(fields) != 3:
                raise ValueError(f'{path}:{number}: want "port frame nanoseconds", got {line.strip()!r}')
            line_port, frame, when = (int(x) for x in fields)
            if line_port == port:
                frames.append((frame, when))
    return frames


def frame_stats(frames, first=None, last=None):
    """The figures (module docstring) for the frames numbered first to last (inclusive, None: open)."""
    kept = [(n, t) for n, t in frames if (first is None or n >= first) and (last is None or n <= last)]
    if len(kept) < 2:
        raise ValueError('fewer than two frames in the range: no interval to measure')
    intervals = [b[1] - a[1] for a, b in zip(kept, kept[1:])]
    if any(i < 0 for i in intervals):
        raise ValueError('the clock goes backwards between frames')
    total = kept[-1][1] - kept[0][1]
    if total == 0:
        raise ValueError('every frame was presented at the same instant')
    slowest = sorted(intervals, reverse=True)[:max(1, math.ceil(len(intervals) / 100))]
    return {
        'frames': len(kept),
        'first_frame': kept[0][0],
        'last_frame': kept[-1][0],
        'seconds': total / NANOSECONDS,
        'average_fps': len(intervals) * NANOSECONDS / total,
        'one_percent_low_fps': len(slowest) * NANOSECONDS / sum(slowest) if sum(slowest) else math.inf,
        'worst_frame_ms': max(intervals) / 1e6,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('log')
    ap.add_argument('--port', type=int, default=1, help='the video out port (default 1, the game\'s)')
    ap.add_argument('--from-frame', type=int, default=None, help='the first frame counted')
    ap.add_argument('--to-frame', type=int, default=None, help='the last frame counted')
    ap.add_argument('--json', action='store_true', help='print the figures as JSON')
    a = ap.parse_args(argv)
    try:
        stats = frame_stats(read_log(a.log, a.port), a.from_frame, a.to_frame)
    except (OSError, ValueError) as e:
        print(f'frame_stats: {e}', file=sys.stderr)
        return 1
    if a.json:
        print(json.dumps(stats, sort_keys=True))
    else:
        print(f"frames {stats['first_frame']}-{stats['last_frame']} ({stats['frames']}) over {stats['seconds']:.2f} s: "
              f"average {stats['average_fps']:.2f} fps, 1% low {stats['one_percent_low_fps']:.2f} fps, "
              f"worst frame {stats['worst_frame_ms']:.1f} ms")
    return 0


if __name__ == '__main__':
    sys.exit(main())
