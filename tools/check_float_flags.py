#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Every game source is compiled so that floating-point operations happen as written.

usage: check_float_flags.py BUILD_DIR

Reads BUILD_DIR/compile_commands.json and checks the compile command of every file under game/:
it must have -ffp-contract=off and -fno-fast-math, and nothing that lets the compiler fuse,
reassociate or otherwise change float operations (-ffast-math, -Ofast, -funsafe-math-optimizations,
-fassociative-math, -freciprocal-math, -ffinite-math-only, -fno-signed-zeros,
-fno-trapping-math, -ffp-contract=fast or on, -ffp-model=fast or aggressive, -fapprox-func,
-fno-honor-nans, -fno-honor-infinities, -menable-unsafe-fp-math). Replacements reproduce the
original's float instructions one by one (STYLE.md, "Floating point"). Exit 0 when every command
passes, 1 when one does not, 2 without a compilation database.
"""
from __future__ import annotations

import json
import os
import shlex
import sys

REQUIRED = ('-ffp-contract=off', '-fno-fast-math')
FORBIDDEN = ('-ffast-math', '-Ofast', '-funsafe-math-optimizations', '-fassociative-math', '-freciprocal-math',
             '-ffinite-math-only', '-fno-signed-zeros', '-fno-trapping-math', '-ffp-contract=fast',
             '-ffp-contract=on', '-ffp-contract=fast-honor-pragmas', '-ffp-model=fast', '-ffp-model=aggressive',
             '-fapprox-func', '-fno-honor-nans', '-fno-honor-infinities', '-menable-unsafe-fp-math')


def flags_of(entry: dict) -> list[str]:
    return entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])


def problems(entries: list[dict], root: str) -> list[str]:
    out = []
    game = os.path.join(root, 'game') + os.sep
    sources = [e for e in entries if os.path.abspath(os.path.join(e.get('directory', ''), e['file'])).startswith(game)]
    if not sources:
        return ['no game sources in the compilation database']
    for e in sources:
        flags = flags_of(e)
        rel = os.path.relpath(os.path.join(e.get('directory', ''), e['file']), root)
        for need in REQUIRED:
            if need not in flags:
                out.append(f'{rel}: missing {need}')
        for bad in FORBIDDEN:
            if bad in flags:
                out.append(f'{rel}: {bad} is not allowed')
    return out


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    db = os.path.join(sys.argv[1], 'compile_commands.json')
    if not os.path.isfile(db):
        print(f'float flags: {db} not found (configure with CMake first)', file=sys.stderr)
        return 2
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(db) as f:
        found = problems(json.load(f), root)
    for p in found:
        print(f'float flags: {p}', file=sys.stderr)
    if found:
        return 1
    print('float flags: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
