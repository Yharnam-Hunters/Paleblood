#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Warns when the capture library lags behind the runtime, or recordings wait for promotion.

usage: capture_status.py [--captures DIR]

Two warnings, printed for tools/end_session.sh (exit code 0 either way; 3 for bad input):
- the library is stale: tools/capture_loop.py last recorded (CAPTURES/.capture-loop.json) before
  the newest boot record in symbols/boot_history.csv, so scenes the runtime now reaches may hold
  inputs nobody recorded;
- edge-verified functions have recordings but no current `recorded` result in
  symbols/verification.csv: promotion was never attempted for their code as it is now (they
  wait for an independent review, or for tools/promote.py).
Nothing is printed without a capture library (a runtime-only setup).
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAMP = '.capture-loop.json'


def newest_boot(root: str) -> str | None:
    with open(os.path.join(root, 'symbols', 'boot_history.csv'), newline='') as f:
        dates = [r['date'] for r in csv.DictReader(f) if r.get('date')]
    return max(dates) if dates else None


def last_capture(captures: str) -> str | None:
    try:
        with open(os.path.join(captures, STAMP)) as f:
            return json.load(f).get('date')
    except (OSError, ValueError):
        return None


def unpromoted(root: str, captures: str) -> list[str]:
    """Edge-verified functions with recordings and no recorded result for their current code."""
    sys.path.insert(0, os.path.join(root, 'tools'))
    import verification
    results = verification.read_csv(os.path.join(root, 'symbols', 'verification.csv'), verification.VERIFICATION_HEADER)
    with open(os.path.join(root, 'symbols', 'functions.csv'), newline='') as f:
        names = [r['name'] for r in csv.DictReader(f) if r['status'] == 'edge-verified']
    out = []
    for name in names:
        directory = os.path.join(captures, name)
        if not os.path.isdir(directory) or not any(n.endswith('.json') for n in os.listdir(directory)):
            continue
        current = verification.source_hash(root, name)
        if not any(r['function'] == name and r['cases'] == 'recorded' and r['source'] == current for r in results):
            out.append(name)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--captures', default=os.environ.get('BB_CAPTURES') or os.path.join(ROOT, '..', 'captures'))
    a = ap.parse_args()
    if not os.path.isdir(a.captures):
        return 0
    boot, captured = newest_boot(ROOT), last_capture(a.captures)
    if boot and (not captured or captured < boot):
        print(f'capture_status: WARNING: the capture library was last recorded {captured or "never (no tools/capture_loop.py run)"}, '
              f'before the newest boot record ({boot}): run tools/capture_loop.py')
    waiting = unpromoted(ROOT, a.captures)
    if waiting:
        print(f'capture_status: WARNING: {len(waiting)} edge-verified function(s) have recordings but no current '
              f'recorded result (review, then tools/promote.py): ' + ', '.join(waiting))
    return 0


if __name__ == '__main__':
    sys.exit(main())
