#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Upgrades edge-verified functions to verified once inputs recorded in the game exist and pass.

usage: promote.py [--captures DIR] [--dry-run]

For each row of symbols/functions.csv with status edge-verified: if the capture library
(--captures, default $BB_CAPTURES, then ../captures) holds recorded cases for it (CAPTURES/NAME/*.json),
runs tools/verify.py on those and on its edge cases (CAPTURES/NAME/edge); when every case passes
on both and the function's independent review is approved (symbols/reviews.csv, or its code is
still grandfathered in tools/readable_allowlist.txt), the status becomes verified. Both runs are
recorded in symbols/verification.csv (verify.py --record; failing cases listed in
symbols/quarantine.csv with their reason don't count as failures), and the function's whole
record must then satisfy tools/validate_functions.py's rule for verified: no unexplained
failure in any case set or option, nothing stale. Nothing changes otherwise; a failure is printed and makes the exit code 1. Without a capture library or an executable it does nothing and says so.
"""
from __future__ import annotations

import argparse
import csv
import glob
import io
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cases(directory: str) -> int:
    return len(glob.glob(os.path.join(directory, '*.json')))


def passes(name: str, directory: str, record: str | None) -> tuple[bool, str]:
    cmd = [sys.executable, os.path.join(ROOT, 'tools', 'verify.py'), 'run', '--function', name, '--captures', directory]
    if record:
        cmd += ['--record', record]
    # verify.py otherwise writes original/replacement result JSON under CAPTURES/results.
    # Keep comparison artifacts separate from the source cases in both normal and dry runs.
    with tempfile.TemporaryDirectory(prefix='paleblood-promote-') as output:
        p = subprocess.run(cmd + ['--out', output], capture_output=True, text=True)
    try:
        r = json.loads(p.stdout)
    except json.JSONDecodeError:
        return False, (p.stderr.strip().splitlines() or ['no output'])[-1]
    n = cases(directory)
    failed = int(r['recorded']['failed']) if record else r['failed']
    held = int(r['recorded']['quarantined']) if record else 0
    ok = p.returncode == 0 and failed == 0 and r['cases'] == n
    return ok, f"{r['passed']}/{n} passed" + (f', {held} quarantined' if held else '')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--captures', default=os.environ.get('BB_CAPTURES') or os.path.join(ROOT, '..', 'captures'))
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    if not os.path.isdir(a.captures):
        print(f'promote: no capture library at {a.captures}; nothing to do')
        return 0
    path = os.path.join(ROOT, 'symbols', 'functions.csv')
    with open(path, newline='') as f:
        text = f.read()
    rows = list(csv.reader(io.StringIO(text)))
    head = rows[0]
    status, name_col = head.index('status'), head.index('name')
    changed, failed = [], False
    for row in rows[1:]:
        if row[status] != 'edge-verified':
            continue
        name = row[name_col]
        recorded = os.path.join(a.captures, name)
        if not cases(recorded):
            continue
        edge = os.path.join(recorded, 'edge')
        if not cases(edge):
            print(f'promote: {name}: recorded cases but no edge cases; left edge-verified')
            continue
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        from validate_functions import check_reviews
        if check_reviews(ROOT, [{'address': row[head.index('address')], 'name': name, 'status': 'verified', '_line': 0}]):
            print(f'promote: {name}: recordings exist, but no approved review yet (symbols/reviews.csv); left edge-verified')
            continue
        record = None if a.dry_run else 'recorded'
        ok_r, msg_r = passes(name, recorded, record)
        ok_e, msg_e = passes(name, edge, record and 'edge') if ok_r else (False, 'not run')
        if ok_r and ok_e and not a.dry_run:
            import verification
            problems = verification.check(ROOT, [{'name': name, 'status': 'verified', '_line': row[0]}])
            if problems:
                print(f'promote: {name}: recorded {msg_r}, edge {msg_e}, but: ' + '; '.join(problems), file=sys.stderr)
                failed = True
                continue
        if ok_r and ok_e:
            print(f'promote: {name}: recorded {msg_r}, edge {msg_e}: verified')
            row[status] = 'verified'
            changed.append(name)
        else:
            print(f'promote: {name}: recorded {msg_r}, edge {msg_e}: stays edge-verified', file=sys.stderr)
            failed = True
    if changed and not a.dry_run:
        out = io.StringIO()
        csv.writer(out, lineterminator='\n').writerows(rows)
        with open(path, 'w', newline='') as f:
            f.write(out.getvalue())
        print(f'promote: {len(changed)} verified; run tools/progress.py --update-readme')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
