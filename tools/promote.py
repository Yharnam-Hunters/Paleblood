#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Upgrades edge-verified functions to verified once inputs recorded in the game exist and pass.

usage: promote.py [--captures DIR] [--dry-run] [--check-unreviewed] [--jobs N]
       promote.py --families [--dry-run]

For each row of symbols/functions.csv with status edge-verified: if the capture library
(--captures, default $BB_CAPTURES, then ../captures) holds recorded cases for it (CAPTURES/NAME/*.json),
runs tools/verify.py on those and on its edge cases (CAPTURES/NAME/edge); when every case passes
on both and the function's independent review is approved (symbols/reviews.csv, or its code is
still grandfathered in tools/readable_allowlist.txt), the status becomes verified. Both runs are
recorded in symbols/verification.csv (verify.py --record; failing cases listed in
symbols/quarantine.csv with their reason don't count as failures), and the function's whole
record must then satisfy tools/validate_functions.py's rule for verified: no unexplained
failure in any case set or option, nothing stale. Nothing changes otherwise; a failure is printed and makes the exit code 1. Without a capture library or an executable it does nothing and says so.

--check-unreviewed also runs the recorded and edge cases of functions that have recordings but no
approved review yet, without recording anything, and lists those that pass as awaiting review
(the next step: an independent review, CONTRIBUTING.md); a failure among them makes the exit
code 1 too.

--families promotes function family members instead (docs/FAMILIES.md): a member that is
edge-verified becomes verified when it has a current, passing edge result, a current recorded result
or a current byte-exact row in symbols/families/<family>.matching.csv (tools/match.py family,
docs/MATCHING.md), and an approved review (symbols/families/<family>.reviews.csv). It runs nothing:
the evidence is already recorded, and tools/validate_functions.py applies the same rule.
"""
from __future__ import annotations

import argparse
import concurrent.futures
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
    """Case files, as verify.py runs them: abi.json is a fixture manifest, not a case."""
    return len([p for p in glob.glob(os.path.join(directory, '*.json')) if os.path.basename(p) != 'abi.json'])


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


def promote_families(dry_run: bool) -> int:
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import families as fam
    import validate_functions
    total = 0
    for family in fam.families(ROOT):
        members = fam.members(ROOT, family['family'])
        evidence = validate_functions.family_evidence(ROOT, family, members)
        changed = waiting = 0
        for m in members:
            member = fam.member_name(family, m['address'])
            if (m['status'] == 'edge-verified' and member in evidence['edge'] and member in evidence['approved']
                    and (member in evidence['recorded'] or member in evidence['matched'])):
                m['status'] = 'verified'
                changed += 1
            elif m['status'] == 'edge-verified':
                waiting += 1
        if changed or waiting:
            print(f"promote: {family['family']}: {changed} verified, {waiting} still edge-verified")
        if changed and not dry_run:
            fam.write_table(fam.members_path(ROOT, family['family']), fam.MEMBER_HEADER, members)
        total += changed
    if total and not dry_run:
        print(f'promote: {total} family members verified; main regenerates progress (tools/progress_pr.py)')
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--captures', default=os.environ.get('BB_CAPTURES') or os.path.join(ROOT, '..', 'captures'))
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--jobs', type=int, default=1, metavar='N', help='functions verified side by side (default 1)')
    ap.add_argument('--check-unreviewed', action='store_true',
                    help='also verify functions with recordings but no approved review, and list those that pass')
    ap.add_argument('--families', action='store_true',
                    help='promote function family members on their recorded evidence instead (no runs)')
    a = ap.parse_args()
    if a.families:
        return promote_families(a.dry_run)
    if not os.path.isdir(a.captures):
        print(f'promote: no capture library at {a.captures}; nothing to do')
        return 0
    path = os.path.join(ROOT, 'symbols', 'functions.csv')
    with open(path, newline='') as f:
        text = f.read()
    rows = list(csv.reader(io.StringIO(text)))
    head = rows[0]
    status, name_col = head.index('status'), head.index('name')
    changed, awaiting, failed = [], [], False
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from validate_functions import check_reviews
    # Which functions to run, and how: reviewed ones record their runs, unreviewed ones (with
    # --check-unreviewed) are only checked.
    work = []
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
        reviewed = not check_reviews(ROOT, [{'address': row[head.index('address')], 'name': name, 'status': 'verified', '_line': 0}])
        if not reviewed and not a.check_unreviewed:
            print(f'promote: {name}: recordings exist, but no approved review yet (symbols/reviews.csv); left edge-verified')
            continue
        work.append((row, name, recorded, edge, reviewed))

    def run(item):
        row, name, recorded, edge, reviewed = item
        record = 'recorded' if reviewed and not a.dry_run else None
        ok_r, msg_r = passes(name, recorded, record)
        ok_e, msg_e = passes(name, edge, record and 'edge') if ok_r else (False, 'not run')
        return ok_r, msg_r, ok_e, msg_e

    # Functions run side by side (--jobs); verify.py --record serializes its writes.
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        results = list(pool.map(run, work))
    for (row, name, recorded, edge, reviewed), (ok_r, msg_r, ok_e, msg_e) in zip(work, results):
        if not reviewed:
            if ok_r and ok_e:
                print(f'promote: {name}: recorded {msg_r}, edge {msg_e}: awaiting an independent review')
                awaiting.append(name)
            else:
                print(f'promote: {name}: recorded {msg_r}, edge {msg_e}: fails (not reviewed yet)', file=sys.stderr)
                failed = True
            continue
        if ok_r and ok_e and not a.dry_run:
            import verification
            # Every function is listed (quarantine rows name other functions too), and only this
            # one's problems count: the rest is tools/validate_functions.py's.
            listed = [{'name': r[name_col], 'status': 'verified' if r is row else r[status], '_line': n + 2}
                      for n, r in enumerate(rows[1:])]
            problems = [p for p in verification.check(ROOT, listed) if f' {name} is verified' in p]
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
        print(f'promote: {len(changed)} verified; main regenerates progress (tools/progress_pr.py)')
    if awaiting:
        print(f'promote: {len(awaiting)} awaiting an independent review: ' + ', '.join(awaiting))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
