#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Require an owned wrong-domain data witness between two correct fixture runs."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys


def main():
    correct, wrong = map(Path, sys.argv[1:])
    before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (correct, wrong)]
    for binary, expected in ((correct, 0), (wrong, 1), (correct, 0)):
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
        assert run.returncode == expected, (run.returncode, expected, run.stdout, run.stderr)
        if expected:
            assert run.stdout == 'temporary_heap_wrong_getter guest_null=1 decoy_preserved=1 allocation_calls=0\n'
            assert re.fullmatch(r'temporary heap TLS line[0-9]+: target->heap==expected\n', run.stderr), run.stderr
        else:
            assert re.fullmatch(r'temporary heap guest TLS: 4 cases on main\+worker; 8 scenarios; [1-9][0-9]* checks passed\n', run.stdout), run.stdout
            assert not run.stderr, run.stderr
    assert before == [hashlib.sha256(p.read_bytes()).hexdigest() for p in (correct, wrong)]
    print('temporary heap compiled control: correct0 wrong1 restored0; owned decoy mismatch')


if __name__ == '__main__':
    main()
