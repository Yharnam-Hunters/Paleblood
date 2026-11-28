#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Frame rate of two builds against each other, with the noise floor of each.

Two runs of one build differ: the same build measured 4.81 to 7.15 fps in Iosefka's Clinic. A
difference between two builds means something only when it is larger than that. `run` starts the
two builds' runs alternately (A1 B1 A2 B2 ...; two rounds each by default), so a slow spell of the
machine falls on both, and measures every run over the same FRAMES: --from-frame to --to-frame of
the same save and input script (by default 1250 to 3000, the clinic walk after the load, up to just
before its first scripted stall). Each build's spread over its rounds is its noise floor: a
difference counts only when it is larger than both builds' spreads (the rule of bbhost's area check,
docs/area-check.md "Reading an A/B").

A fixed range of frames, not a fixed stretch of time: the game stalls for seconds at scripted points
(in the clinic walk a 5-7 s stall somewhere in frames 3050-3350, another in 4220-4530; the frame
moves with the build), and a time window ends at an earlier frame for a slower build, so its window
can end before a stall that a faster build's window holds, and the slower build's 1% low looks
better. Over the same frames both builds meet the same stalls. A range that holds a scripted stall
can leave its frames out with --skip-frames (FIRST-LAST, wide enough for where both builds stall):
their intervals are reported on their own, the count, time and longest. A build that stalls
anywhere else still shows it in its figures.

Work of other programs while a run is measured disturbs it. While a run plays, the machine's load is
sampled every second: the load average (/proc/loadavg), and the CPUs busy with work other than the
run's own (all CPU time in /proc/stat less the time of the run's processes, the session the run
starts). A run whose other work between its first and last measured frame rises above
--max-other-cpus is left out of the figures, with the reason. Builds and other GPU work belong
outside the measured frames: the run command builds before it starts the game, and holds the
machine's GPU lock while it plays (as tools/run_game.sh does).

The run command is a shell command per build; {log} in it becomes the frame log the run must write
(pbboot/boot.py --frame-log, runtime/display.h) and {tag} the run's name (a1, b1, a2, ...).

Usage:
  tools/ab_bench.py run --a 'CMD' --b 'CMD' --out DIR [--rounds 2] [--from-frame N] [--to-frame N]
                        [--skip-frames A-B,C-D] [--max-other-cpus N]
  tools/ab_bench.py compare --out DIR [same frame options]   (the report again from DIR's logs)

A run must reach --to-frame (give its command a long enough timeout); one that does not is left out.

DIR gets each run's frame log (<tag>.frames.log) and load samples (<tag>.load.json), then ab.json
and ab.txt.
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frame_stats  # noqa: E402

NANOSECONDS = 1e9
SAMPLE_SECONDS = 1.0
BUILDS = ('a', 'b')


DEFAULT_FROM_FRAME = 1250
DEFAULT_TO_FRAME = 3000
DEFAULT_SKIP_FRAMES = ''


def parse_frame_ranges(text):
    """'A-B,C-D' as [(A, B), (C, D)], each inclusive; '' gives no range. ValueError for a range that
    is not two frame numbers, the first not above the second."""
    ranges = []
    for part in filter(None, (p.strip() for p in text.split(','))):
        first, sep, last = part.partition('-')
        if not sep or not first.strip().isdigit() or not last.strip().isdigit():
            raise ValueError(f'{part!r}: want FIRST-LAST, two frame numbers')
        first, last = int(first), int(last)
        if first > last:
            raise ValueError(f'{part!r}: the first frame is above the last')
        ranges.append((first, last))
    return ranges


def range_intervals(frames, first, last, skip=()):
    """The intervals between consecutive frames numbered first to last, split by the frame each ends
    on: (measured, skipped, start, end), the first two lists of nanoseconds, skipped those ending on
    a frame inside a skip range, and the times of the first and last frame. ValueError when the run
    never presented frame `first` or ended before frame `last`."""
    kept = [(n, t) for n, t in frames if first <= n <= last]
    if not kept or kept[0][0] != first:
        raise ValueError(f'no frame {first}: the run did not reach the first measured frame')
    if kept[-1][0] != last:
        raise ValueError(f'the run ended before frame {last}, the last measured frame')
    measured, skipped = [], []
    for (_, earlier), (frame, later) in zip(kept, kept[1:]):
        inside = any(lo <= frame <= hi for lo, hi in skip)
        (skipped if inside else measured).append(later - earlier)
    return measured, skipped, kept[0][1], kept[-1][1]


def busy_ticks(stat_text):
    """The CPU time spent busy, all CPUs together, in clock ticks: /proc/stat's first line less idle
    and I/O wait."""
    fields = stat_text.split('\n', 1)[0].split()
    if not fields or fields[0] != 'cpu':
        raise ValueError('not /proc/stat: its first line is not the "cpu" total')
    ticks = [int(x) for x in fields[1:9]]  # user nice system idle iowait irq softirq steal
    return sum(ticks) - ticks[3] - ticks[4]


def process_ticks(stat_text):
    """A process's session and its own CPU time (user and system) in clock ticks, from its
    /proc/<pid>/stat."""
    fields = stat_text[stat_text.rindex(')') + 2:].split()  # after "pid (comm) "
    session = int(fields[3])
    return session, int(fields[11]) + int(fields[12])  # utime stime


def session_ticks(session):
    """The CPU time of each live process of one session, by pid."""
    ticks_by_pid = {}
    for pid in os.listdir('/proc'):
        if not pid.isdigit():
            continue
        try:
            with open(f'/proc/{pid}/stat') as f:
                sid, ticks = process_ticks(f.read())
        except (OSError, ValueError):
            continue  # it ended meanwhile
        if sid == session:
            ticks_by_pid[int(pid)] = ticks
    return ticks_by_pid


def other_cpus(previous, current, ticks_per_second):
    """The CPUs busy with work other than the run's between two samples, each a (seconds, machine
    busy ticks, the run's ticks by pid) triple. The run's time is what each of its processes alive at
    the second sample added since the first (all of it for one started since); a process that ended
    between them takes its last ticks with it, which count as other work then. Never below 0."""
    seconds = current[0] - previous[0]
    if seconds <= 0:
        return 0.0
    own = sum(ticks - previous[2].get(pid, 0) for pid, ticks in current[2].items())
    other = (current[1] - previous[1]) - own
    return max(0.0, other / (seconds * ticks_per_second))


class LoadSampler(threading.Thread):
    """Samples the machine's load every second while a run plays: the time on the frame log's clock
    (CLOCK_MONOTONIC, as runtime/display.h stamps frames), the 1-minute load average and the CPUs
    busy with other work."""

    def __init__(self, session):
        super().__init__(daemon=True)
        self.session, self.samples, self.done = session, [], threading.Event()
        self.ticks_per_second = os.sysconf('SC_CLK_TCK')

    def read(self):
        with open('/proc/stat') as f:
            busy = busy_ticks(f.read())
        return time.monotonic(), busy, session_ticks(self.session)

    def run(self):
        previous = self.read()
        while not self.done.wait(SAMPLE_SECONDS):
            current = self.read()
            with open('/proc/loadavg') as f:
                load = float(f.read().split()[0])
            self.samples.append({'ns': int(current[0] * NANOSECONDS), 'loadavg': load,
                                 'other_cpus': round(other_cpus(previous, current, self.ticks_per_second), 3)})
            previous = current


def run_one(command, tag, out):
    """Runs one build's command for the run `tag`, sampling the load; its exit status."""
    log = os.path.join(out, f'{tag}.frames.log')
    shell = command.replace('{log}', log).replace('{tag}', tag)
    process = subprocess.Popen(shell, shell=True, start_new_session=True)
    sampler = LoadSampler(process.pid)
    sampler.start()
    status = process.wait()
    sampler.done.set()
    sampler.join()
    with open(os.path.join(out, f'{tag}.load.json'), 'w') as f:
        json.dump({'command': shell, 'status': status, 'samples': sampler.samples}, f)
    return status


def measure_run(out, tag, a):
    """One run's figures over the measured frames and its load there, or the reason it is left out."""
    result = {'tag': tag}
    try:
        frames = frame_stats.read_log(os.path.join(out, f'{tag}.frames.log'), a.port)
        measured, skipped, start, end = range_intervals(frames, a.from_frame, a.to_frame, a.skip)
        result.update(frame_stats.interval_figures(measured))
    except (OSError, ValueError) as e:
        return {**result, 'left_out': str(e)}
    result['skipped_intervals'] = len(skipped)
    result['skipped_seconds'] = sum(skipped) / NANOSECONDS
    result['skipped_worst_ms'] = max(skipped) / 1e6 if skipped else 0.0
    try:
        with open(os.path.join(out, f'{tag}.load.json')) as f:
            samples = [s for s in json.load(f)['samples'] if start <= s['ns'] <= end]
    except (OSError, ValueError, KeyError):
        samples = []
    if not samples:
        return {**result, 'left_out': 'no load samples between its first and last measured frame'}
    result['max_loadavg'] = max(s['loadavg'] for s in samples)
    result['max_other_cpus'] = max(s['other_cpus'] for s in samples)
    if result['max_other_cpus'] > a.max_other_cpus:
        result['left_out'] = f"other work used {result['max_other_cpus']:.1f} CPUs while it was measured (limit {a.max_other_cpus:g})"
    return result


def verdict(a_values, b_values):
    """The difference of the means (B less A), each build's spread, and whether the difference counts:
    each build needs two measured runs, and the difference must be larger than both spreads."""
    if len(a_values) < 2 or len(b_values) < 2:
        return {'counts': False, 'why': 'fewer than two measured runs of a build: no noise floor'}
    spread_a, spread_b = max(a_values) - min(a_values), max(b_values) - min(b_values)
    mean_a, mean_b = sum(a_values) / len(a_values), sum(b_values) / len(b_values)
    difference = mean_b - mean_a
    counts = abs(difference) > max(spread_a, spread_b)
    return {'mean_a': mean_a, 'mean_b': mean_b, 'difference': difference, 'spread_a': spread_a,
            'spread_b': spread_b, 'noise_floor': max(spread_a, spread_b), 'counts': counts,
            'why': 'larger than both spreads' if counts else 'within the noise floor'}


FIGURES = ('average_fps', 'one_percent_low_fps', 'worst_frame_ms', 'skipped_seconds')


def frames_line(a):
    skip = ', '.join(f'{lo}-{hi}' for lo, hi in a.skip)
    return f'frames {a.from_frame}-{a.to_frame}' + (f', skipped frames {skip} reported apart' if skip else '')


def report(out, a, tags):
    """Measures every run in DIR, writes ab.json and ab.txt and returns the text."""
    runs = [measure_run(out, tag, a) for tag in tags]
    measured = {b: [r for r in runs if r['tag'].startswith(b) and 'left_out' not in r] for b in BUILDS}
    keys = FIGURES if a.skip else FIGURES[:-1]
    figures = {key: verdict([r[key] for r in measured['a']], [r[key] for r in measured['b']]) for key in keys}
    with open(os.path.join(out, 'ab.json'), 'w') as f:
        json.dump({'frames': {'from': a.from_frame, 'to': a.to_frame, 'skip': a.skip},
                   'runs': runs, 'figures': figures}, f, indent=1, sort_keys=True)
    lines = [frames_line(a)]
    for r in runs:
        if 'left_out' in r:
            lines.append(f"{r['tag']}: left out: {r['left_out']}")
        else:
            skipped = (f"; skipped {r['skipped_intervals']} frames, {r['skipped_seconds']:.2f} s, worst "
                       f"{r['skipped_worst_ms']:.1f} ms" if a.skip else '')
            lines.append(f"{r['tag']}: {r['average_fps']:.2f} fps, 1% low {r['one_percent_low_fps']:.2f}, worst "
                         f"{r['worst_frame_ms']:.1f} ms{skipped}; load avg <= {r['max_loadavg']:.1f}, "
                         f"other work <= {r['max_other_cpus']:.1f} CPUs")
    for key, v in figures.items():
        if 'difference' not in v:
            lines.append(f'{key}: {v["why"]}')
            continue
        lines.append(f"{key}: A {v['mean_a']:.2f}, B {v['mean_b']:.2f}, B - A {v['difference']:+.2f}; spread A {v['spread_a']:.2f}, "
                     f"B {v['spread_b']:.2f}; noise floor {v['noise_floor']:.2f}: {'COUNTS' if v['counts'] else v['why']}")
    text = '\n'.join(lines) + '\n'
    with open(os.path.join(out, 'ab.txt'), 'w') as f:
        f.write(text)
    return text


def run_tags(out):
    """The runs DIR holds, in the order they ran."""
    tags = [name[:-len('.frames.log')] for name in os.listdir(out) if name.endswith('.frames.log')]
    return sorted(tags, key=lambda t: (int(t[1:]) if t[1:].isdigit() else 0, t))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='action', required=True)
    for name in ('run', 'compare'):
        p = sub.add_parser(name)
        p.add_argument('--out', required=True, help='the folder of the runs\' logs and the report')
        p.add_argument('--from-frame', type=int, default=DEFAULT_FROM_FRAME,
                       help=f'the first measured frame (default {DEFAULT_FROM_FRAME})')
        p.add_argument('--to-frame', type=int, default=DEFAULT_TO_FRAME,
                       help=f'the last measured frame (default {DEFAULT_TO_FRAME}); a run must reach it')
        p.add_argument('--skip-frames', default=DEFAULT_SKIP_FRAMES,
                       help='FIRST-LAST[,...]: frames left out of the figures and reported apart, the game\'s '
                            'scripted stalls (default none)')
        p.add_argument('--max-other-cpus', type=float, default=2.0,
                       help='CPUs of other work allowed while a run is measured before it is left out (default 2)')
        p.add_argument('--port', type=int, default=1, help='the video out port (default 1, the game\'s)')
        if name == 'run':
            p.add_argument('--a', required=True, help='build A\'s run command ({log}: its frame log, {tag}: the run)')
            p.add_argument('--b', required=True, help='build B\'s run command')
            p.add_argument('--rounds', type=int, default=2, help='runs of each build, alternating (default 2)')
    a = ap.parse_args(argv)
    try:
        a.skip = parse_frame_ranges(a.skip_frames)
    except ValueError as e:
        ap.error(f'--skip-frames: {e}')
    if a.from_frame >= a.to_frame:
        ap.error('--from-frame must be below --to-frame')
    if a.action == 'run':
        if a.rounds < 2:
            ap.error('--rounds below 2 gives no noise floor')
        os.makedirs(a.out, exist_ok=True)
        tags = []
        for round_number in range(1, a.rounds + 1):
            for build in BUILDS:
                tag = f'{build}{round_number}'
                status = run_one(getattr(a, build), tag, a.out)
                print(f'ab_bench: {tag} exited {status}', file=sys.stderr)
                tags.append(tag)
    else:
        tags = run_tags(a.out)
    sys.stdout.write(report(a.out, a, tags))
    return 0


if __name__ == '__main__':
    sys.exit(main())
