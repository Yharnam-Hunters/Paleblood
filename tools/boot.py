#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Runs the boot harness (runtime/boot.c) on your executable and records the result.

usage: boot.py [--elf EBOOT.elf] [--nids NID_DB.xml] [--timeout S] [--trace] [--no-record]

The game's own system modules listed in game/modules.csv with action "map" are mapped next to the
executable (from the same directory: NAME.elf), and the executable's imports they export are
bound to them.

Writes, from the run:
  symbols/boot.json           where the boot stops, the furthest boot milestone and the import
                              counts (read by tools/progress.py)
  symbols/boot_history.csv    a dated row whenever boot progress or its failure location changes;
                              includes the outcome, phase, return site, and guest fault details
  symbols/imports.csv         every import of the executable and the mapped modules: importer,
                              library, symbol, kind, provider (a module, "runtime", or empty);
                              tools/runtime_issues.py groups it
  symbols/module_functions.csv the mapped modules' functions the executable or another module
                              uses: tracked like game functions (status original), replaceable later
Prints "milestone reached: NAME" when the run gets further than the last recorded one.

--elf defaults to $BB_ELF, then $BB_DATA_ROOT/elf/eboot.elf. --nids (a public NID database, for
names) defaults to $BB_NID_DB, then GhidraOrbis's nid_db.xml under ../tools. The harness binary
is $BB_PBBOOT or build/runtime/pbboot.
"""
from __future__ import annotations

import argparse
import csv
import datetime
import glob
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HISTORY_FIELDS = ('date', 'milestone', 'milestone_index', 'imports_total', 'imports_bundled', 'imports_implemented',
                  'imports_remaining', 'stops_at', 'outcome', 'phase', 'returns_to', 'fault_at', 'fault_signal',
                  'fault_address')
KEPT = ('phase', 'outcome', 'milestone', 'milestone_index', 'milestones', 'imports_total', 'imports_implemented',
        'imports_bundled', 'imports_remaining', 'imports_called', 'modules', 'first_unimplemented', 'fault')
IMPORT_FIELDS = ('importer', 'library', 'symbol', 'kind', 'provider')
MODULE_FIELDS = ('module', 'offset', 'size', 'symbol', 'status')


def default_elf() -> str:
    root = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    return os.environ.get('BB_ELF') or os.path.join(root, 'elf', 'eboot.elf')


def default_nids() -> str | None:
    if os.environ.get('BB_NID_DB'):
        return os.environ['BB_NID_DB']
    hits = sorted(glob.glob(os.path.join(ROOT, '..', 'tools', 'ghidra_*', 'Ghidra', 'Extensions', 'GhidraOrbis',
                                         'data', 'nid_db.xml')))
    return hits[-1] if hits else None


def mapped_modules(root: str) -> list[str]:
    path = os.path.join(root, 'game', 'modules.csv')
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        return [r['module'] for r in csv.DictReader(f) if r['action'] == 'map']


def stops_at(b: dict) -> str:
    s = b.get('first_unimplemented')
    if s:
        return f"{s['caller']}->{s['library']}:{s['symbol'] or s['nid']}"
    if b.get('fault'):
        return f"{b['outcome']} at {b['fault']['at']}"
    return b['outcome']


def record(status: dict, root: str, today: str) -> str | None:
    """Writes boot.json and the history row; returns the milestone name if it advanced."""
    kept = {k: status[k] for k in KEPT if k in status}
    with open(os.path.join(root, 'symbols', 'boot.json'), 'w') as f:
        json.dump(kept, f, indent=2, sort_keys=True)
        f.write('\n')
    path = os.path.join(root, 'symbols', 'boot_history.csv')
    rows = []
    if os.path.isfile(path):
        with open(path, newline='') as f:
            rows = list(csv.DictReader(f))
    first = kept.get('first_unimplemented') or {}
    fault = kept.get('fault') or {}
    row = {
        'date': today,
        'stops_at': stops_at(kept),
        'outcome': kept.get('outcome', ''),
        'phase': kept.get('phase', ''),
        'returns_to': first.get('returns_to', ''),
        'fault_at': fault.get('at', ''),
        'fault_signal': fault.get('signal', ''),
        'fault_address': fault.get('address', ''),
    }
    for k in ('milestone', 'milestone_index', 'imports_total', 'imports_bundled', 'imports_implemented',
              'imports_remaining'):
        row[k] = str(kept.get(k, 0))
    last = rows[-1] if rows else None
    if not last or any(last.get(k, '') != row[k] for k in HISTORY_FIELDS[1:]):
        rows.append(row)
        with open(path, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=HISTORY_FIELDS, lineterminator='\n', restval='')
            w.writeheader()
            w.writerows({k: r.get(k, '') for k in HISTORY_FIELDS} for r in rows)
    if last and int(row['milestone_index']) > int(last['milestone_index']):
        return row['milestone']
    return None


def write_listing(listing: str, root: str) -> None:
    """imports.csv and module_functions.csv from `pbboot --imports` (tab-separated: importer,
    library, symbol, kind, provider, provider offset, size)."""
    imports, functions = set(), {}
    for line in listing.splitlines():
        if not line:
            continue
        importer, library, symbol, kind, provider, offset, size = line.split('\t')
        imports.add((importer, library, symbol, kind, provider))
        if provider and provider != 'runtime':
            functions[(provider, offset)] = (provider, offset, size, symbol, 'original')
    with open(os.path.join(root, 'symbols', 'imports.csv'), 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(IMPORT_FIELDS)
        w.writerows(sorted(imports))
    path = os.path.join(root, 'symbols', 'module_functions.csv')
    # a status other than original (a replacement, later) is kept
    old = {}
    if os.path.isfile(path):
        with open(path, newline='') as f:
            old = {(r['module'], r['offset']): r['status'] for r in csv.DictReader(f)}
    with open(path, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(MODULE_FIELDS)
        for key in sorted(functions):
            m, off, size, sym, status = functions[key]
            w.writerow((m, off, size, sym, old.get(key, status)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--elf', default=None)
    ap.add_argument('--nids', default=None)
    ap.add_argument('--timeout', type=int, default=30)
    ap.add_argument('--trace', action='store_true')
    ap.add_argument('--no-record', action='store_true', help='run and print only')
    a = ap.parse_args()
    elf = a.elf or default_elf()
    pbboot = os.environ.get('BB_PBBOOT') or os.path.join(ROOT, 'build', 'runtime', 'pbboot')
    for path, what in ((elf, 'executable (your own dump\'s eboot.elf)'), (pbboot, 'boot harness: build the repository')):
        if not os.path.isfile(path):
            print(f'boot: {path}: no {what}', file=sys.stderr)
            return 3
    objects = [elf]
    for name in mapped_modules(ROOT):
        path = os.path.join(os.path.dirname(elf), f'{name}.elf')
        if not os.path.isfile(path):
            print(f'boot: {path}: no module (tools/prepare_dump.sh writes the game\'s modules next to eboot.elf)',
                  file=sys.stderr)
            return 3
        objects += ['--module', path]
    nids = a.nids or default_nids()
    names = ['--nids', nids] if nids else []
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, 'status.json')
        cmd = [pbboot, *objects, '--status', out, '--timeout', str(a.timeout), *names]
        if a.trace:
            cmd.append('--trace')
        # The guest runs in this process's child; a hard limit covers a runaway harness.
        p = subprocess.run(cmd, timeout=a.timeout + 60)
        if p.returncode != 0 or not os.path.isfile(out):
            print(f'boot: harness failed (exit {p.returncode})', file=sys.stderr)
            return 3
        with open(out) as f:
            status = json.load(f)
    if a.no_record:
        return 0
    listing = subprocess.run([pbboot, *objects, '--imports', *names], capture_output=True, text=True, timeout=120)
    if listing.returncode:
        print(f'boot: listing imports failed: {listing.stderr.strip()}', file=sys.stderr)
        return 3
    write_listing(listing.stdout, ROOT)
    reached = record(status, ROOT, datetime.date.today().isoformat())
    if reached:
        print(f'milestone reached: {reached}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
