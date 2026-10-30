#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Merge rows (from tools/ghidra/ExportNames.java) into symbols/functions.csv.

usage: merge_functions.py ROWS.csv [--root DIR]

New addresses are added; a row for an address already present must agree on the name, or the
merge stops and lists the conflicts (rename in the CSV on purpose, not by merging). The
result is sorted by address and checked with tools/validate_functions.py; on a failure
functions.csv is left as it was. The file is replaced in one step (a temporary file renamed over
it), so an error part way never leaves it half written.
"""
from __future__ import annotations

import argparse
import csv
import io
import os
import subprocess
import sys
import tempfile

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = ['address', 'size', 'name', 'system', 'status', 'notes']


def read(path: str) -> list[dict]:
    with open(path, newline='') as f:
        rd = csv.DictReader(f)
        if rd.fieldnames != HEADER:
            raise SystemExit(f'merge: {path}: header must be {",".join(HEADER)}')
        rows = list(rd)
    for n, r in enumerate(rows, start=2):
        if None in r or None in r.values():
            # A row with more fields than the header (an unquoted comma in its notes) or fewer.
            raise SystemExit(f'merge: {path}:{n}: expected {len(HEADER)} fields; quote a field that has a comma')
    return rows


def replace_file(path: str, text: str) -> None:
    """Writes text to path in one step: a temporary file next to it, renamed over it."""
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix='.functions-', suffix='.csv')
    try:
        with os.fdopen(fd, 'w', newline='') as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('rows')
    ap.add_argument('--root', default=DEFAULT_ROOT)
    a = ap.parse_args()
    target = os.path.join(a.root, 'symbols', 'functions.csv')
    cur = {r['address']: r for r in read(target)}
    conflicts, added = [], 0
    for r in read(a.rows):
        have = cur.get(r['address'])
        if have is None:
            cur[r['address']] = r
            added += 1
        elif have['name'] != r['name']:
            conflicts.append(f"{r['address']}: functions.csv has {have['name']}, rows have {r['name']}")
    if conflicts:
        print('merge: conflicting names, nothing written:', *conflicts, sep='\n  ', file=sys.stderr)
        return 1
    with open(target, newline='') as f:
        before = f.read()
    out = io.StringIO()
    w = csv.DictWriter(out, fieldnames=HEADER, lineterminator='\n')
    w.writeheader()
    for k in sorted(cur, key=lambda x: int(x, 16)):
        w.writerow(cur[k])
    replace_file(target, out.getvalue())
    v = subprocess.run([sys.executable, os.path.join(DEFAULT_ROOT, 'tools', 'validate_functions.py'), '--root', a.root])
    if v.returncode:
        replace_file(target, before)
        print('merge: validation failed, functions.csv restored', file=sys.stderr)
        return 1
    print(f'merge: {added} row(s) added')
    return 0


if __name__ == '__main__':
    sys.exit(main())
