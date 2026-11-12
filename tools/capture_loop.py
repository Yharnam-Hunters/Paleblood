#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Records every reachable scene into the capture library, then promotes what passes.

usage: capture_loop.py [--captures DIR] [--scenes FILE] [--scene NAME]... [--skip-boots]

The whole verification loop of docs/VERIFY.md ("Recording in the game") in one command:

1. One capture boot per scene (tools/boot.py --capture): a scene is a way into the game the
   runtime reaches, such as a new game through character creation, or a save checkpoint. The
   scenes are listed in a JSON file outside the repository (--scenes, default
   $BB_CAPTURE_SCENES, then CAPTURES/scenes.json), since checkpoints are your own save data:

       {"scenes": [
         {"name": "new-game", "seconds": 130, "keyboard_text": "Hunter",
          "input_script": "26:down*0.12,28:cross*0.4,..."},
         {"name": "clinic", "seconds": 200, "savedata_from": "/path/to/checkpoint",
          "input_script": "..."}]}

   Each boot holds the GPU lock ($BB_GPU_LOCK, default $BB_DATA_ROOT/gpu.lock, as
   tools/run_game.sh) for its whole run.
2. The edge cases of every function with recordings are written next to them
   (CAPTURES/NAME/edge, from the generator beside the replacement: game/<system>/<file>_cases.py).
3. tools/promote.py --check-unreviewed: functions with an approved review become verified when
   every recorded and edge case passes; those without one are verified without recording
   anything and listed as awaiting an independent review.
4. CAPTURES/.capture-loop.json notes the date, the source commit and the scenes, which
   tools/capture_status.py (run by tools/end_session.sh) compares with the newest boot record.

Exit code: 0, or 1 when a boot failed or a recorded function fails its cases (printed).
"""
from __future__ import annotations

import argparse
import datetime
import fcntl
import json
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAMP = '.capture-loop.json'


def default_captures() -> str:
    return os.environ.get('BB_CAPTURES') or os.path.join(ROOT, '..', 'captures')


def default_lock() -> str:
    data_root = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    return os.environ.get('BB_GPU_LOCK') or os.path.join(data_root, 'gpu.lock')


def read_scenes(path: str) -> list[dict]:
    with open(path) as f:
        scenes = json.load(f)['scenes']
    for s in scenes:
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', str(s.get('name', ''))):
            raise ValueError(f'{path}: every scene needs a filename-safe "name"')
        if not isinstance(s.get('seconds'), int) or s['seconds'] <= 0:
            raise ValueError(f'{path}: scene {s["name"]}: "seconds" must be a positive whole number')
    return scenes


def boot_command(scene: dict, captures: str, frames: str | None) -> list[str]:
    cmd = [sys.executable, os.path.join(ROOT, 'tools', 'boot.py'), '--no-record', '--capture', captures,
           '--timeout', str(scene['seconds']), '--display', scene.get('display', 'headless')]
    if frames:
        cmd += ['--display-capture', frames]
    for key, flag in (('input_script', '--input-script'), ('keyboard_text', '--keyboard-text'),
                      ('savedata_from', '--savedata-from'), ('capture_functions', '--capture-functions'),
                      ('capture_cases', '--capture-cases')):
        if key in scene:
            cmd += [flag, str(scene[key])]
    return cmd


def hold_lock(path: str, wait: int):
    """The GPU lock, held until the returned file is closed; None when it stays busy."""
    f = open(path, 'a')
    deadline = time.monotonic() + wait
    while True:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return f
        except BlockingIOError:
            if time.monotonic() >= deadline:
                f.close()
                return None
            time.sleep(5)


def run_scene(scene: dict, captures: str, lock: str, wait: int) -> bool:
    print(f'capture_loop: scene {scene["name"]}: {scene["seconds"]} s', flush=True)
    held = hold_lock(lock, wait)
    if not held:
        print(f'capture_loop: scene {scene["name"]}: the GPU lock {lock} stayed busy; skipped', file=sys.stderr)
        return False
    with held, tempfile.TemporaryDirectory(prefix='paleblood-capture-frames-') as frames:
        log = os.path.join(captures, f'.scene-{scene["name"]}.log')
        with open(log, 'w') as out:
            p = subprocess.run(boot_command(scene, captures, frames if scene.get('frames') else None),
                               stdout=out, stderr=subprocess.STDOUT, timeout=scene['seconds'] + 600)
    ok = p.returncode == 0
    print(f'capture_loop: scene {scene["name"]}: ' + ('done' if ok else f'boot failed (exit {p.returncode}, {log})'),
          file=sys.stdout if ok else sys.stderr, flush=True)
    return ok


def replacement_sources(root: str) -> dict[str, str]:
    """Function name -> the game/ file that defines its replacement bb_NAME."""
    out = {}
    definition = re.compile(r'^(?:extern "C"[^\n]*?)?\bbb_([a-z0-9_]+)\s*\(', re.M)
    for directory, _, files in os.walk(os.path.join(root, 'game')):
        for name in files:
            if not name.endswith(('.cpp', '.c')):
                continue
            path = os.path.join(directory, name)
            with open(path, errors='replace') as f:
                for m in definition.finditer(f.read()):
                    out.setdefault(m.group(1), path)
    return out


def edge_generators(root: str, name: str, system: str, source: str | None) -> list[str]:
    """The edge-case generators that may hold the function's cases, most specific first: named
    after the function in its system's directory (game/<system>/<name without the system
    prefix>_cases.py), beside the replacement's file, any generator next to the replacement whose
    docstring names the replacement's file, then the other generators next to it. One generator
    may serve several functions (robust_heap_cases.py writes the robust heap's allocations and its
    free, picked by a variant), so write_edge_cases keeps only cases for the function's address."""
    candidates = []
    if name.startswith(system + '_'):
        candidates.append(os.path.join(root, 'game', system, name[len(system) + 1:] + '_cases.py'))
    if source:
        candidates.append(os.path.splitext(source)[0] + '_cases.py')
        directory, filename = os.path.split(source)
        others = sorted(os.path.join(directory, g) for g in os.listdir(directory) if g.endswith('_cases.py'))
        candidates += [g for g in others if filename in generator_doc(g)]
        candidates += others
    found = []
    for c in candidates:
        if os.path.isfile(c) and c not in found:
            found.append(c)
    return found


def generator_doc(path: str) -> str:
    """A generator's docstring, the place its usage line and the files it covers are written."""
    with open(path, errors='replace') as f:
        m = re.search(r'"""(.*?)"""', f.read(4000), re.S)
    return m.group(1) if m else ''


def generator_variants(path: str) -> list[str | None]:
    """The variants a generator's usage line offers (usage: X_cases.py OUT_DIR [a|b|c]), or the
    generator's one default when it lists none."""
    m = re.search(r'^usage:\s*\S+\s+OUT_DIR\s+\[([A-Za-z0-9_|]+)\]', generator_doc(path), re.M)
    return m.group(1).split('|') if m else [None]


def case_address(path: str) -> int | None:
    try:
        with open(path) as f:
            return int(str(json.load(f).get('address')), 16)
    except (OSError, ValueError, AttributeError, json.JSONDecodeError):
        return None


def write_edge_cases(name: str, address: str, generators: list[str], captures: str) -> str | None:
    """Regenerates CAPTURES/NAME/edge with the cases its generators write for the function's own
    address; a problem, or None. A generator for several functions is run with each variant its
    usage line lists; its cases are taken from anywhere in its output (some write a capture
    library, OUT/<function>/edge), and only cases for this address are kept, so a function is
    never checked against another function's cases."""
    if not generators:
        return 'no edge-case generator found'
    want = int(address, 16)
    edge = os.path.join(captures, name, 'edge')
    with tempfile.TemporaryDirectory(prefix='.edge-', dir=os.path.join(captures, name)) as scratch:
        fresh = os.path.join(scratch, 'edge')
        tried, failures = [], []
        for generator in generators:
            for variant in generator_variants(generator):
                # Generators make their output directory themselves: write a new one per run.
                out = os.path.join(scratch, f'run{len(tried)}')
                tried.append(os.path.relpath(generator, ROOT) + (f' {variant}' if variant else ''))
                p = subprocess.run([sys.executable, generator, out] + ([variant] if variant else []),
                                   capture_output=True, text=True)
                if p.returncode or not os.path.isdir(out):
                    # A neighbour that fails is not this function's generator; reported only when
                    # no generator writes the function's cases.
                    failures.append(f'{tried[-1]} failed: ' + (p.stderr.strip().splitlines() or ['no output'])[-1])
                    continue
                mine = [os.path.join(d, c) for d, _, files in os.walk(out) for c in files
                        if c.endswith('.json') and case_address(os.path.join(d, c)) == want]
                if mine:
                    os.mkdir(fresh)
                    for case in mine:
                        target = os.path.join(fresh, os.path.basename(case))
                        if os.path.exists(target):
                            return f'{tried[-1]}: two cases for {address} named {os.path.basename(case)}'
                        os.rename(case, target)
                    break
            if os.path.isdir(fresh):
                break
        if not os.path.isdir(fresh):
            return f'no generator writes cases for {address} (tried {", ".join(tried)})' + \
                ''.join(f'; {f}' for f in failures)
        if os.path.isdir(edge):
            for old in os.listdir(edge):
                if old.endswith('.json'):
                    os.remove(os.path.join(edge, old))
            if os.listdir(edge):
                return f'{edge} holds files other than cases; left as it is'
            os.rmdir(edge)
        os.rename(fresh, edge)
    return None


def recorded_functions(captures: str, names: set[str]) -> list[str]:
    return sorted(n for n in names
                  if os.path.isdir(os.path.join(captures, n))
                  and any(f.endswith('.json') for f in os.listdir(os.path.join(captures, n))))


def edge_verified(root: str) -> dict[str, tuple[str, str]]:
    """Each edge-verified function's system and address."""
    import csv
    with open(os.path.join(root, 'symbols', 'functions.csv'), newline='') as f:
        return {r['name']: (r['system'], r['address'])
                for r in csv.DictReader(f) if r['status'] == 'edge-verified'}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--captures', default=default_captures(), help='the capture library (default $BB_CAPTURES, then ../captures)')
    ap.add_argument('--scenes', default=None, help='the scene list (default $BB_CAPTURE_SCENES, then CAPTURES/scenes.json)')
    ap.add_argument('--scene', action='append', default=[], metavar='NAME', help='record only these scenes')
    ap.add_argument('--skip-boots', action='store_true', help='record nothing: edge cases and promotion only')
    ap.add_argument('--lock', default=default_lock(), help='the GPU lock (default $BB_GPU_LOCK, then $BB_DATA_ROOT/gpu.lock)')
    ap.add_argument('--lock-wait', type=int, default=1800, metavar='S', help='how long to wait for the GPU lock per scene')
    a = ap.parse_args()
    captures = os.path.abspath(a.captures)
    if captures.startswith(os.path.realpath(ROOT) + os.sep):
        print('capture_loop: the capture library holds inputs from your own dump; keep it outside the repository',
              file=sys.stderr)
        return 3
    os.makedirs(captures, exist_ok=True)
    failed = False
    covered = []
    if not a.skip_boots:
        scenes_path = a.scenes or os.environ.get('BB_CAPTURE_SCENES') or os.path.join(captures, 'scenes.json')
        try:
            scenes = read_scenes(scenes_path)
        except (OSError, ValueError, KeyError, json.JSONDecodeError) as e:
            print(f'capture_loop: cannot read the scene list: {e}', file=sys.stderr)
            return 3
        unknown = set(a.scene) - {s['name'] for s in scenes}
        if unknown:
            print(f'capture_loop: no such scene: {", ".join(sorted(unknown))}', file=sys.stderr)
            return 3
        for scene in scenes:
            if a.scene and scene['name'] not in a.scene:
                continue
            if run_scene(scene, captures, a.lock, a.lock_wait):
                covered.append(scene['name'])
            else:
                failed = True
        commit = subprocess.run(['git', '-C', ROOT, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
        with open(os.path.join(captures, STAMP), 'w') as f:
            json.dump({'date': datetime.date.today().isoformat(), 'commit': commit, 'scenes': covered}, f, indent=1)
            f.write('\n')

    candidates = edge_verified(ROOT)
    recorded = recorded_functions(captures, set(candidates))
    sources = replacement_sources(ROOT)
    for name in recorded:
        system, address = candidates[name]
        problem = write_edge_cases(name, address, edge_generators(ROOT, name, system, sources.get(name)), captures)
        if problem:
            print(f'capture_loop: {name}: edge cases: {problem}', file=sys.stderr)
    print(f'capture_loop: {len(recorded)} of {len(candidates)} edge-verified functions have recordings', flush=True)
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'promote.py'), '--captures', captures,
                        '--check-unreviewed', '--jobs', str(max(1, (os.cpu_count() or 2) // 2))])
    if p.returncode:
        failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
