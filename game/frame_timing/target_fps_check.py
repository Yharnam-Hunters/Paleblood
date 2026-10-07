#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check BB_TARGET_FPS on the frame limiter replacement, outside the game.

usage: target_fps_check.py BOOT.bin LIB.so CASE.json

Runs the replacement on one case under each setting and prints what the limiter did: the
frame interval and sync interval it left in the SprjFlipper object and how often it read the
clock (the wait). Unset must match the original; 30, 60 and uncapped are our option.
"""
import json
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run(boot, lib, case, target):
    env = dict(os.environ)
    env.pop('BB_TARGET_FPS', None)
    if target:
        env['BB_TARGET_FPS'] = target
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'harness.py'), '--boot', boot, '--case', case,
                        '--address', '0x02434770', '--replacement', 'bb_frame_timing_pace_frame', '--lib', lib],
                       capture_output=True, text=True, env=env, timeout=60)
    if r.returncode:
        sys.exit(f'harness failed for {target}: {r.stderr}')
    return json.loads(r.stdout)


def field(result, case, off, size):
    """Final bytes of the flipper at `off`: the case's bytes with the run's writes applied."""
    data = bytearray(bytes.fromhex(case['buffers']['flipper']['bytes']))
    for w in result['writes']:
        if w['addr'].startswith('buf:flipper+'):
            o = int(w['addr'].split('+')[1], 16)
            b = bytes.fromhex(w['bytes'])
            data[o:o + len(b)] = b
    return bytes(data[off:off + size])


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    boot, lib, path = sys.argv[1:]
    case = json.load(open(path))
    for target in (None, '30', '60', 'uncapped'):
        r = run(boot, lib, path, target)
        interval = struct.unpack('<f', field(r, case, 0x18, 4))[0]
        sync = struct.unpack('<I', field(r, case, 0x10, 4))[0]
        reads = sum(1 for c in r['calls'] if c.get('import') == 'gettimeofday')
        print(f"{target or 'unset':9} interval {interval:.6f} s ({1 / interval:5.1f} FPS)  sync {sync}  clock reads {reads}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
