#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Batch differential verification of a function family: `tools/verify.py family`.

Every member runs its edge cases (game/<system>/family_cases.py) on the original and on the
family's replacement, exactly as `verify.py run` compares one function; the difference is only
in how the work is scheduled. Members are split into shards, and each shard runs as one harness
process per side (tools/harness.py --plan): the image is mapped and the game library loaded once
per process, and every case still runs in its own fork of it.

Mutation test: on a sample of members (spread over the table, and at least one per distinct
parameter value), every mutant the family lists (test/family_mutants.c), and for a parameterized
family the replacement of a neighbouring value, replaces the real body. Each must fail at least
one case of every sampled member; one that passes is reported and fails the run.

--record writes one row per member to symbols/families/<family>.verification.csv (the
symbols/verification.csv format, case set `edge`) and moves passing members from `replaced` to
`edge-verified`. A run with any failing case or surviving mutant records nothing.
"""
from __future__ import annotations

import datetime
import json
import os
import subprocess
import sys
import tempfile

import families as fam
import verification

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS = os.path.join(ROOT, 'tools', 'harness.py')
SCHEMA = 1
MAX_FAILURES_REPORTED = 10


class FamilyError(Exception):
    pass


def family_row(root: str, name: str) -> dict:
    rows = [f for f in fam.families(root) if f['family'] == name]
    if not rows:
        raise FamilyError(f'{name} has no row in symbols/families.csv')
    return rows[0]


def sample_members(members: list[dict], size: int) -> list[dict]:
    """About SIZE members spread evenly over the table, plus the first member of every distinct
    value not already in the sample."""
    if not members:
        return []
    step = max(1, len(members) // max(1, size))
    picked = {m['address']: m for m in members[::step]}
    seen = {m['value'] for m in picked.values()}
    for m in members:
        if m['value'] not in seen:
            picked[m['address']] = m
            seen.add(m['value'])
    return sorted(picked.values(), key=lambda m: int(m['address'], 16))


def sibling_symbol(family: dict, values: list[str], value: str) -> str | None:
    """The replacement of the next distinct value (the previous one for the last value)."""
    if not value or len(values) < 2:
        return None
    i = values.index(value)
    other = values[i + 1] if i + 1 < len(values) else values[i - 1]
    return fam.replacement(family, other)


def run_shards(elf: str, entries: list[dict], jobs: int, timeout: int) -> dict:
    """Run plan entries across up to JOBS harness processes; {key: result}."""
    shards = [entries[i::jobs] for i in range(jobs) if entries[i::jobs]]
    results: dict = {}
    with tempfile.TemporaryDirectory(prefix='pb-family-') as tmp:
        procs = []
        for n, shard in enumerate(shards):
            path = os.path.join(tmp, f'plan{n}.json')
            with open(path, 'w') as f:
                json.dump(shard, f)
            out = open(os.path.join(tmp, f'out{n}.json'), 'w+')
            err = open(os.path.join(tmp, f'err{n}.txt'), 'w+')
            cmd = [sys.executable, HARNESS, '--elf', elf, '--plan', path, '--timeout', str(timeout)]
            procs.append((subprocess.Popen(cmd, stdout=out, stderr=err), out, err))
        for proc, out, err in procs:
            code = proc.wait()
            out.seek(0)
            err.seek(0)
            if code != 0:
                for other, _, _ in procs:
                    if other.poll() is None:
                        other.kill()
                        other.wait()
                raise FamilyError(f'harness exited {code}: {err.read().strip()[-400:]}')
            results.update(json.load(out))
            out.close()
            err.close()
    return results


def compare_member(compare, address: str, case_ids: list[str], results: dict, original_key, other_key) -> dict:
    def doc(key) -> dict:
        return {'schema': SCHEMA, 'address': address,
                'cases': [dict(results[key(cid)], id=cid) for cid in case_ids]}
    return compare(doc(original_key), doc(other_key))


def verify_family(name: str, elf: str, lib: str, mutants_lib: str, *, root: str = ROOT, jobs: int = 8,
                  sample: int = 64, timeout: int = 20, record: bool = False, compare=None) -> dict:
    family = family_row(root, name)
    members = fam.members(root, name)
    if not members:
        raise FamilyError(f'{name} has no members (symbols/families/{name}.csv)')
    generator = fam.case_module(root, family['system'])
    values = sorted({m['value'] for m in members if m['value']}, key=lambda v: int(v, 16))
    member_cases: dict[str, list[dict]] = {}
    entries = []
    for m in members:
        value = int(m['value'], 16) if m['value'] else None
        cases = generator.cases(name, m['address'], value)
        member_cases[m['address']] = cases
        symbol = fam.replacement(family, m['value'])
        for case in cases:
            key = f"{m['address']}/{case['id']}"
            entries.append({'key': f'{key}/original', 'address': m['address'], 'case': case})
            entries.append({'key': f'{key}/replacement', 'address': m['address'], 'case': case,
                            'replacement': symbol, 'lib': lib})
    sampled = sample_members(members, sample)
    mutants: dict[str, set[str]] = {}       # mutant label -> sampled member addresses
    for m in sampled:
        tried = [(s, mutants_lib) for s in generator.MUTANTS.get(name, [])]
        sibling = sibling_symbol(family, values, m['value'])
        if sibling:
            tried.append((sibling, lib))
        for symbol, library in tried:
            label = 'neighbouring value' if library == lib else symbol
            mutants.setdefault(label, set()).add(m['address'])
            for case in member_cases[m['address']]:
                entries.append({'key': f"{m['address']}/{case['id']}/mutant/{label}", 'address': m['address'],
                                'case': case, 'replacement': symbol, 'lib': library})
    if not mutants:
        raise FamilyError(f'{name} lists no mutants (game/{family["system"]}/family_cases.py MUTANTS)')
    results = run_shards(elf, entries, jobs, timeout)

    per_member = {}
    failures = []
    for m in members:
        a = m['address']
        ids = [c['id'] for c in member_cases[a]]
        summary = compare_member(compare, a, ids, results, lambda cid: f'{a}/{cid}/original',
                                 lambda cid: f'{a}/{cid}/replacement')
        per_member[a] = summary
        for f in summary['failures']:
            if len(failures) < MAX_FAILURES_REPORTED:
                failures.append({'member': a, **f})
    mutant_report = []
    for label, addresses in sorted(mutants.items()):
        killed = 0
        survivors = []
        for a in sorted(addresses):
            ids = [c['id'] for c in member_cases[a]]
            summary = compare_member(compare, a, ids, results, lambda cid: f'{a}/{cid}/original',
                                     lambda cid: f'{a}/{cid}/mutant/{label}')
            if summary['failed']:
                killed += 1
            else:
                survivors.append(a)
        mutant_report.append({'mutant': label, 'members': len(addresses), 'killed': killed,
                              'survivors': survivors[:MAX_FAILURES_REPORTED]})
    total = sum(s['cases'] for s in per_member.values())
    failed = sum(s['failed'] for s in per_member.values())
    report = {'schema': SCHEMA, 'function': f'family:{name}', 'family': name, 'members': len(members),
              'members_failed': sum(1 for s in per_member.values() if s['failed']),
              'cases': total, 'passed': total - failed, 'failed': failed, 'failures': failures,
              'mutants': mutant_report, 'sampled': len(sampled), 'source': fam.source_hash(root, family)}
    clean = failed == 0 and all(r['killed'] == r['members'] for r in mutant_report)
    if record and clean:
        record_family(root, family, members, per_member, report['source'])
        report['recorded'] = len(members)
    return report


def record_family(root: str, family: dict, members: list[dict], per_member: dict, source: str) -> None:
    today = datetime.date.today().isoformat()
    rows = []
    for m in members:
        s = per_member[m['address']]
        rows.append({'function': fam.member_name(family, m['address']), 'cases': 'edge', 'option': '',
                     'total': str(s['cases']), 'passed': str(s['passed']), 'failed': str(s['failed']),
                     'quarantined': '0', 'source': source, 'date': today})
        if m['status'] == 'replaced' and not s['failed']:
            m['status'] = 'edge-verified'
    name = family['family']
    fam.write_table(fam.verification_path(root, name), verification.VERIFICATION_HEADER, rows)
    fam.write_table(fam.members_path(root, name), fam.MEMBER_HEADER, members)
