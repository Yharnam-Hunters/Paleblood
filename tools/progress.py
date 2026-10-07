#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Progress numbers, derived only from files under symbols/.

Denominator: symbols/ghidra_functions.csv (address,size), the functions Ghidra
finds in the executable blocks of the target eboot (see tools/ghidra/).
Numerator: rows of symbols/functions.csv with status replaced or verified
(verified is a subset of replaced).

usage: progress.py [--root DIR] [--out FILE] [--update-readme] [--check] [--status-line]
  (no flag)        print the progress JSON
  --out FILE       write the JSON to FILE
  --status-line    print the current-status sentence (plain text, for issues and descriptions)
  --update-readme  rewrite the status, progress and target blocks of README.md, the status
                   block of CONTRIBUTING.md (and the progress
                   block of STATUS.md when it has one)
  --check          exit 1 if those blocks differ from the generated ones
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMPLEMENTED = ('replaced', 'verified')
BLOCKS = ('progress', 'target')


def sha256_file(path: str) -> str | None:
    if not os.path.isfile(path):
        return None
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def read_csv(path: str) -> list[dict]:
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


def pct(part: int, whole: int) -> float | None:
    return round(100.0 * part / whole, 2) if whole else None


def compute(root: str) -> dict:
    export = read_csv(os.path.join(root, 'symbols', 'ghidra_functions.csv'))
    funcs = read_csv(os.path.join(root, 'symbols', 'functions.csv'))
    total_f = len(export)
    total_b = sum(int(r['size']) for r in export)

    def tally(rows: list[dict]) -> dict:
        return {'functions': len(rows), 'bytes': sum(int(r['size']) for r in rows)}

    impl = [r for r in funcs if r['status'] in IMPLEMENTED]
    ver = [r for r in funcs if r['status'] == 'verified']
    systems: dict[str, dict] = {}
    for r in funcs:
        s = systems.setdefault(r['system'], {'tracked': 0, 'replaced': 0, 'verified': 0,
                                             'replaced_bytes': 0})
        s['tracked'] += 1
        if r['status'] in IMPLEMENTED:
            s['replaced'] += 1
            s['replaced_bytes'] += int(r['size'])
        if r['status'] == 'verified':
            s['verified'] += 1
    return {
        'schema': 1,
        'slices': slices(export, funcs),
        'inputs': {
            'ghidra_functions_sha256': sha256_file(os.path.join(root, 'symbols', 'ghidra_functions.csv')),
            'functions_sha256': sha256_file(os.path.join(root, 'symbols', 'functions.csv')),
        },
        'total': {'functions': total_f, 'bytes': total_b},
        'tracked': tally(funcs),
        'replaced': {**tally(impl), 'functions_pct': pct(len(impl), total_f),
                     'bytes_pct': pct(tally(impl)['bytes'], total_b)},
        'verified': {**tally(ver), 'functions_pct': pct(len(ver), total_f),
                     'bytes_pct': pct(tally(ver)['bytes'], total_b)},
        'systems': {k: systems[k] for k in sorted(systems)},
    }


SLICES = 64


def slices(export: list[dict], funcs: list[dict]) -> list[dict]:
    """The exported code range in SLICES equal address ranges: functions and bytes in each
    (by entry address), and how many of those bytes are replaced and verified."""
    if not export:
        return []
    starts = [int(r['address'], 16) for r in export]
    lo = min(starts)
    hi = max(int(r['address'], 16) + int(r['size']) for r in export)
    step = -(-(hi - lo) // SLICES)
    out = [{'start': f'0x{lo + i * step:08x}', 'end': f'0x{min(hi, lo + (i + 1) * step):08x}',
            'functions': 0, 'bytes': 0, 'replaced_bytes': 0, 'verified_bytes': 0} for i in range(SLICES)]
    for r in export:
        sl = out[(int(r['address'], 16) - lo) // step]
        sl['functions'] += 1
        sl['bytes'] += int(r['size'])
    for r in funcs:
        a = int(r['address'], 16)
        if not lo <= a < hi:
            continue
        sl = out[(a - lo) // step]
        if r['status'] in IMPLEMENTED:
            sl['replaced_bytes'] += int(r['size'])
        if r['status'] == 'verified':
            sl['verified_bytes'] += int(r['size'])
    return out


def fmt_pct(p: float | None) -> str:
    return 'n/a' if p is None else f'{p:.2f}%'


def render_progress(d: dict) -> str:
    if not d['total']['functions']:
        return ('No Ghidra export committed yet (`symbols/ghidra_functions.csv`), '
                'so there is no denominator.\n')
    t, r, v = d['total'], d['replaced'], d['verified']
    return '\n'.join([
        '| | Functions | Bytes |',
        '|---|---:|---:|',
        f"| Target (Ghidra export) | {t['functions']} | {t['bytes']} |",
        f"| Replaced | {r['functions']} ({fmt_pct(r['functions_pct'])}) | "
        f"{r['bytes']} ({fmt_pct(r['bytes_pct'])}) |",
        f"| Verified | {v['functions']} ({fmt_pct(v['functions_pct'])}) | "
        f"{v['bytes']} ({fmt_pct(v['bytes_pct'])}) |",
        '',
        'Generated by `tools/progress.py --update-readme` from `symbols/`. Do not edit.',
        '',
    ])


def status_sentence(d: dict, link: bool) -> str:
    """Where the project stands, in one sentence: never "is a native port", only what runs today."""
    v, t = d['verified'], d['total']
    runtime = '[bbport](third_party/)' if link else 'bbport'
    return (f"Today it runs on {runtime}, which loads the original executable with our replacements "
            f"hooked in: {v['functions']} of {t['functions']} functions are verified "
            f"({fmt_pct(v['functions_pct'])}).")


def render_status(d: dict) -> str:
    return status_sentence(d, True) + '\n'


def render_target(root: str) -> str:
    path = os.path.join(root, 'target.sha256')
    if not os.path.isfile(path):
        return 'No `target.sha256` yet.\n'
    with open(path) as f:
        lines = [ln.rstrip('\n') for ln in f if ln.strip()]
    return '```\n' + '\n'.join(lines) + '\n```\n'


def block_re(name: str) -> re.Pattern:
    return re.compile(rf'(<!-- {name}:start -->\n)(.*?)(<!-- {name}:end -->)', re.S)


def apply_blocks(text: str, bodies: dict[str, str]) -> str:
    for name, body in bodies.items():
        pat = block_re(name)
        if not pat.search(text):
            raise SystemExit(f'progress: README.md has no {name} block')
        text = pat.sub(lambda m: m.group(1) + body + m.group(3), text)
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=DEFAULT_ROOT)
    ap.add_argument('--out')
    ap.add_argument('--update-readme', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--status-line', action='store_true')
    a = ap.parse_args()

    data = compute(a.root)
    if a.status_line:
        print(status_sentence(data, False))
        return 0
    js = json.dumps(data, indent=2, sort_keys=True) + '\n'
    if a.out:
        with open(a.out, 'w') as f:
            f.write(js)
    if not (a.update_readme or a.check):
        if not a.out:
            sys.stdout.write(js)
        return 0

    bodies = {'status': render_status(data), 'progress': render_progress(data), 'target': render_target(a.root)}
    stale = []
    for name, blocks in (('README.md', bodies), ('STATUS.md', {'progress': bodies['progress']}),
                         ('CONTRIBUTING.md', {'status': bodies['status']})):
        path = os.path.join(a.root, name)
        if not os.path.isfile(path):
            continue
        with open(path) as f:
            cur = f.read()
        blocks = {k: b for k, b in blocks.items() if (name == 'README.md' and k != 'status') or block_re(k).search(cur)}
        if not blocks:
            continue
        new = apply_blocks(cur, blocks)
        if new == cur:
            continue
        if a.update_readme:
            with open(path, 'w') as f:
                f.write(new)
            print(f'progress: {name} updated')
        else:
            stale.append(name)
    if stale:
        print(f"progress: generated block stale in {', '.join(stale)}; run tools/progress.py --update-readme",
              file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
