#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""runtime/, test/ and tools/ must contain nothing specific to the game: no
title identity, no tracked function names, no tracked addresses. All of that
lives under game/ and symbols/.

usage: check_agnostic.py [--root DIR]
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import sys

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCOPE = ('runtime', 'test', 'tools')
SKIP_EXT = {'.pyc'}


def words(root: str) -> list[tuple[str, re.Pattern]]:
    out = []
    path = os.path.join(root, 'game', 'agnostic_words.txt')
    if os.path.isfile(path):
        with open(path) as f:
            for ln in f:
                w = ln.strip()
                if w and not w.startswith('#'):
                    out.append((w, re.compile(re.escape(w), re.I)))
    for rel, col, label in (('symbols/functions.csv', 'name', 'function name'),
                            ('symbols/functions.csv', 'address', 'address'),
                            ('game/hooks.csv', 'address', 'address')):
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            continue
        with open(p, newline='') as f:
            for r in csv.DictReader(f):
                v = r[col]
                out.append((f'{label} {v}', re.compile(rf'(?<![0-9A-Za-z_]){re.escape(v)}(?![0-9A-Za-z_])', re.I)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=DEFAULT_ROOT)
    root = ap.parse_args().root
    pats = words(root)
    errs = []
    for top in SCOPE:
        for dp, _, fs in os.walk(os.path.join(root, top)):
            if '__pycache__' in dp:
                continue
            for fn in fs:
                if os.path.splitext(fn)[1] in SKIP_EXT:
                    continue
                path = os.path.join(dp, fn)
                rel = os.path.relpath(path, root)
                try:
                    text = open(path, encoding='utf-8').read()
                except (UnicodeDecodeError, OSError):
                    continue
                for n, ln in enumerate(text.splitlines(), 1):
                    for label, pat in pats:
                        if pat.search(ln):
                            errs.append(f'{rel}:{n}: game-specific ({label})')
    for e in errs:
        print(f'agnostic: {e}', file=sys.stderr)
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
