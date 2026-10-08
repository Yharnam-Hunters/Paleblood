#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The verification record: which case sets a function's replacement passed, and the quarantine.

symbols/verification.csv   one row per function, case set and option: the latest
                           `tools/verify.py run --record` of it. Counts only, never case data.
  function     the functions.csv name
  cases        recorded (recorded in a game run), edge (the generated edge cases), or
               option-recorded (recorded in a game run with the option on)
  option       the option the run checked ('' for none: unpatched, no option environment)
  total, passed, failed, quarantined
               failed counts failing cases that are not quarantined; quarantined counts failing
               cases that are (a quarantined case that passes counts as passed)
  source       a hash of the replacement's source (its defining files and every header they
               include from game/ and runtime/include/, recursively): results for other code
               are stale
  date         the day of the run

symbols/quarantine.csv     cases that cannot be used, each with the reason.
  function, cases, option, case (the case file's name without .json), reason, date

A function may be verified only when it has current recorded and edge rows without an option,
and none of its current rows has a failure that is not quarantined (docs/VERIFY.md,
"Quarantine"); tools/validate_functions.py enforces it.
"""
from __future__ import annotations

import csv
import datetime
import fcntl
import hashlib
import io
import os
import re

VERIFICATION_HEADER = ['function', 'cases', 'option', 'total', 'passed', 'failed', 'quarantined', 'source', 'date']
QUARANTINE_HEADER = ['function', 'cases', 'option', 'case', 'reason', 'date']
CASE_SETS = ('recorded', 'edge', 'option-recorded')
SOURCE_HASH_LENGTH = 16
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def defining_files(root: str, name: str) -> list[str]:
    """Files under game/ that define the replacement bb_<name>."""
    out = []
    pattern = re.compile(rf'\bbb_{re.escape(name)}\s*\(')
    for dp, _, fns in os.walk(os.path.join(root, 'game')):
        for fn in sorted(fns):
            if fn.endswith(('.c', '.cc', '.cpp')):
                path = os.path.join(dp, fn)
                with open(path, errors='replace') as f:
                    if pattern.search(f.read()):
                        out.append(os.path.relpath(path, root).replace(os.sep, '/'))
    return sorted(out)


def source_files(root: str, name: str) -> list[str]:
    """The defining files and every header they include from game/ or runtime/include/."""
    search = [os.path.join(root, 'game'), os.path.join(root, 'runtime', 'include')]
    seen: set[str] = set()
    todo = [os.path.join(root, p) for p in defining_files(root, name)]
    while todo:
        path = os.path.normpath(todo.pop())
        if path in seen or not os.path.isfile(path):
            continue
        seen.add(path)
        with open(path, errors='replace') as f:
            text = f.read()
        for inc in INCLUDE.findall(text):
            for base in [os.path.dirname(path)] + search:
                candidate = os.path.normpath(os.path.join(base, inc))
                if os.path.isfile(candidate):
                    todo.append(candidate)
                    break
    return sorted(os.path.relpath(p, root).replace(os.sep, '/') for p in seen)


def source_hash(root: str, name: str) -> str:
    files = source_files(root, name)
    if not files:
        return ''
    h = hashlib.sha256()
    for rel in files:
        h.update(rel.encode() + b'\0')
        with open(os.path.join(root, rel), 'rb') as f:
            h.update(f.read() + b'\0')
    return h.hexdigest()[:SOURCE_HASH_LENGTH]


def read_csv(path: str, header: list[str]) -> list[dict]:
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        rows = list(csv.reader(f))
    if not rows or rows[0] != header:
        raise ValueError(f'{path}: header must be {",".join(header)}')
    return [dict(zip(header, r), _line=n) for n, r in enumerate(rows[1:], 2)]


def write_csv(path: str, header: list[str], rows: list[dict]) -> None:
    out = io.StringIO()
    w = csv.writer(out, lineterminator='\n')
    w.writerow(header)
    for r in rows:
        w.writerow([r[k] for k in header])
    with open(path, 'w', newline='') as f:
        f.write(out.getvalue())


def quarantined(root: str, function: str, cases: str, option: str) -> dict[str, str]:
    """case -> reason for one function, case set and option."""
    rows = read_csv(os.path.join(root, 'symbols', 'quarantine.csv'), QUARANTINE_HEADER)
    return {r['case']: r['reason'] for r in rows
            if r['function'] == function and r['cases'] == cases and r['option'] == option}


def record(root: str, function: str, cases: str, option: str, case_names: list[str], failing: list[str]) -> dict:
    """Writes the row for one run; returns it."""
    if cases not in CASE_SETS:
        raise ValueError(f'case set must be one of {", ".join(CASE_SETS)}')
    q = quarantined(root, function, cases, option)
    failed = [c for c in failing if c not in q]
    row = {'function': function, 'cases': cases, 'option': option, 'total': str(len(case_names)),
           'passed': str(len(case_names) - len(failing)), 'failed': str(len(failed)),
           'quarantined': str(len(failing) - len(failed)), 'source': source_hash(root, function),
           'date': datetime.date.today().isoformat()}
    path = os.path.join(root, 'symbols', 'verification.csv')
    # Runs may record at the same time (one per function): read, update and write under a lock.
    with open(path + '.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rows = [r for r in read_csv(path, VERIFICATION_HEADER)
                if (r['function'], r['cases'], r['option']) != (function, cases, option)]
        rows.append(row)
        rows.sort(key=lambda r: (r['function'], CASE_SETS.index(r['cases']) if r['cases'] in CASE_SETS else 99,
                                 r['option']))
        write_csv(path, VERIFICATION_HEADER, rows)
    return row


def check(root: str, funcs: list[dict]) -> list[str]:
    """Errors for verified functions without a clean, current record, and for malformed rows."""
    errs: list[str] = []
    try:
        results = read_csv(os.path.join(root, 'symbols', 'verification.csv'), VERIFICATION_HEADER)
        quarantine = read_csv(os.path.join(root, 'symbols', 'quarantine.csv'), QUARANTINE_HEADER)
    except ValueError as e:
        return [str(e)]
    names = {f['name'] for f in funcs}
    for q in quarantine:
        where = f"symbols/quarantine.csv:{q['_line']}"
        if q['function'] not in names:
            errs.append(f"{where}: {q['function']} is not in symbols/functions.csv")
        if q['cases'] not in CASE_SETS:
            errs.append(f"{where}: cases must be one of {', '.join(CASE_SETS)}")
        if not q['reason'].strip():
            errs.append(f"{where}: a quarantined case needs its reason")
        if not q['case'].strip():
            errs.append(f"{where}: case is empty")
    counts: dict[tuple, int] = {}
    for q in quarantine:
        key = (q['function'], q['cases'], q['option'])
        counts[key] = counts.get(key, 0) + 1
    by_function: dict[str, list[dict]] = {}
    for r in results:
        where = f"symbols/verification.csv:{r['_line']}"
        if r['cases'] not in CASE_SETS:
            errs.append(f"{where}: cases must be one of {', '.join(CASE_SETS)}")
            continue
        try:
            total, passed, failed, held = (int(r[k]) for k in ('total', 'passed', 'failed', 'quarantined'))
        except ValueError:
            errs.append(f'{where}: total, passed, failed and quarantined must be numbers')
            continue
        if passed + failed + held != total:
            errs.append(f'{where}: passed + failed + quarantined must equal total')
        if held > counts.get((r['function'], r['cases'], r['option']), 0):
            errs.append(f'{where}: {held} quarantined, but symbols/quarantine.csv lists fewer cases for it')
        by_function.setdefault(r['function'], []).append(r)
    for f in funcs:
        if f['status'] != 'verified':
            continue
        where = f"symbols/functions.csv:{f['_line']}: {f['name']} is verified"
        current = source_hash(root, f['name'])
        rows = by_function.get(f['name'], [])
        stale = [r for r in rows if r['source'] != current]
        fresh = [r for r in rows if r['source'] == current]
        for need in ('recorded', 'edge'):
            if not any(r['cases'] == need and r['option'] == '' for r in fresh):
                errs.append(f"{where} without a current {need} result in symbols/verification.csv "
                            f"(tools/verify.py run --record {need})")
        for r in fresh:
            if int(r['failed']) or int(r['total']) == 0:
                label = r['cases'] + (f" with {r['option']}" if r['option'] else '')
                errs.append(f"{where} with {r['failed']} unexplained failure(s) in its {label} cases "
                            f"(symbols/verification.csv:{r['_line']}): fix them or quarantine them with the reason")
        for r in stale:
            label = r['cases'] + (f" with {r['option']}" if r['option'] else '')
            errs.append(f"{where} but its {label} result (symbols/verification.csv:{r['_line']}) is for other code: "
                        f"run it again with --record")
    return errs
