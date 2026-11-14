#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Frame rate of two builds against each other, with the noise floor of each.

Two runs of one build differ: the same build measured 4.81 to 7.15 fps in Iosefka's Clinic. A
difference between two builds means something only when it is larger than that. `run` starts the
two builds' runs alternately (A1 B1 A2 B2 ...; two rounds each by default), so a slow spell of the
machine falls on both, and measures each run over the same window: a fixed settle, then a fixed
measure window, counted from the moment the run presents its anchor frame (a frame number reached
at the same point of the same save, for the clinic the first frames after the load). Each build's
spread over its rounds is its noise floor: a difference counts only when it is larger than both
builds' spreads (the rule of bbhost's area check, docs/area-check.md "Reading an A/B").

Work of other programs during the window disturbs it. While a run plays, the machine's load is
sampled every second: the load average (/proc/loadavg), and the CPUs busy with work other than the
run's own (all CPU time in /proc/stat less the time of the run's processes, the session the run
starts). A run whose other work in its window rises above --max-other-cpus is left out of the
figures, with the reason. Builds and other GPU work belong outside the window: the run command
builds before it starts the game, and holds the machine's GPU lock while it plays (as
tools/run_game.sh does).

The run command is a shell command per build; {log} in it becomes the frame log the run must write
(pbboot/boot.py --frame-log, runtime/display.h) and {tag} the run's name (a1, b1, a2, ...).

Usage:
  tools/ab_bench.py run --a 'CMD' --b 'CMD' --out DIR [--rounds 2] [--anchor-frame N]
                        [--settle S] [--measure S] [--max-other-cpus N]
  tools/ab_bench.py compare --out DIR [same window options]   (the report again from DIR's logs)

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


def window_frames(frames, anchor_frame, settle, measure):
    """The (frame, nanoseconds) pairs presented from settle to settle + measure seconds after the
    anchor frame, with the window's bounds in nanoseconds. ValueError when the run never presented
    the anchor frame or ended before the window did."""
    anchor = next((t for n, t in frames if n >= anchor_frame), None)
    if anchor is None:
        raise ValueError(f'no frame {anchor_frame}: the run did not reach the anchor')
    start, end = anchor + settle * NANOSECONDS, anchor + (settle + measure) * NANOSECONDS
    if not frames or frames[-1][1] < end:
        raise ValueError('the run ended before its measure window did')
    return [(n, t) for n, t in frames if start <= t <= end], start, end


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
    """One run's figures over its window and its load there, or the reason it is left out."""
    result = {'tag': tag}
    try:
        frames = frame_stats.read_log(os.path.join(out, f'{tag}.frames.log'), a.port)
        kept, start, end = window_frames(frames, a.anchor_frame, a.settle, a.measure)
        stats = frame_stats.frame_stats(kept)
    except (OSError, ValueError) as e:
        return {**result, 'left_out': str(e)}
    result.update(stats)
    try:
        with open(os.path.join(out, f'{tag}.load.json')) as f:
            samples = [s for s in json.load(f)['samples'] if start <= s['ns'] <= end]
    except (OSError, ValueError, KeyError):
        samples = []
    if not samples:
        return {**result, 'left_out': 'no load samples in its window'}
    result['max_loadavg'] = max(s['loadavg'] for s in samples)
    result['max_other_cpus'] = max(s['other_cpus'] for s in samples)
    if result['max_other_cpus'] > a.max_other_cpus:
        result['left_out'] = f"other work used {result['max_other_cpus']:.1f} CPUs in its window (limit {a.max_other_cpus:g})"
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


def report(out, a, tags):
    """Measures every run in DIR, writes ab.json and ab.txt and returns the text."""
    runs = [measure_run(out, tag, a) for tag in tags]
    measured = {b: [r for r in runs if r['tag'].startswith(b) and 'left_out' not in r] for b in BUILDS}
    figures = {key: verdict([r[key] for r in measured['a']], [r[key] for r in measured['b']])
               for key in ('average_fps', 'one_percent_low_fps')}
    with open(os.path.join(out, 'ab.json'), 'w') as f:
        json.dump({'window': {'anchor_frame': a.anchor_frame, 'settle': a.settle, 'measure': a.measure},
                   'runs': runs, 'figures': figures}, f, indent=1, sort_keys=True)
    lines = [f'window: {a.settle:g} s settle, then {a.measure:g} s measured, from frame {a.anchor_frame}']
    for r in runs:
        if 'left_out' in r:
            lines.append(f"{r['tag']}: left out: {r['left_out']}")
        else:
            lines.append(f"{r['tag']}: {r['average_fps']:.2f} fps, 1% low {r['one_percent_low_fps']:.2f}, worst "
                         f"{r['worst_frame_ms']:.1f} ms; load avg <= {r['max_loadavg']:.1f}, other work <= {r['max_other_cpus']:.1f} CPUs")
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
        p.add_argument('--anchor-frame', type=int, default=800, help='the frame the window is counted from (default 800)')
        p.add_argument('--settle', type=float, default=60, help='seconds after the anchor before measuring (default 60)')
        p.add_argument('--measure', type=float, default=120, help='seconds measured (default 120)')
        p.add_argument('--max-other-cpus', type=float, default=2.0,
                       help='CPUs of other work allowed in a window before the run is left out (default 2)')
        p.add_argument('--port', type=int, default=1, help='the video out port (default 1, the game\'s)')
        if name == 'run':
            p.add_argument('--a', required=True, help='build A\'s run command ({log}: its frame log, {tag}: the run)')
            p.add_argument('--b', required=True, help='build B\'s run command')
            p.add_argument('--rounds', type=int, default=2, help='runs of each build, alternating (default 2)')
    a = ap.parse_args(argv)
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
