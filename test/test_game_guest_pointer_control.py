#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A compiled wrong-slot run needs its owned data mismatch, bounded by correct runs."""
import hashlib
from pathlib import Path
import subprocess
import sys

def main():
    host, correct, wrong = map(Path, sys.argv[1:])
    paths = [host, correct, wrong]
    before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    for library, mode, expected in [(correct, 'correct', 0), (wrong, 'wrong-slot', 1), (correct, 'correct', 0)]:
        run = subprocess.run([str(host), str(library), mode], capture_output=True, text=True, timeout=15)
        if run.returncode != expected:
            raise AssertionError((run.returncode, expected, run.stdout, run.stderr))
        if expected:
            assert 'guest_pointer_wrong_slot returned=1 owned_decoy=1 mismatch=1\n' in run.stdout
            assert run.stderr == 'bbgame: 1 hook(s) installed\n', run.stderr
        else:
            assert 'guest_pointer_canonical main=1 workers=2 rebound=1 unqualified_rejected=1\n' in run.stdout
    assert before == [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    print('guest pointer compiled control: correct0 wrong1 restored0; owned wrong-slot data witness')


if __name__ == '__main__':
    main()
