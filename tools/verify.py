#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential tester: original function vs replacement, same captured input.

Design: docs/VERIFY.md.

usage:
  verify.py run --function NAME --captures DIR [--boot BOOT.bin] [--lib LIB.so] [--out DIR]
  verify.py compare ORIGINAL.json REPLACEMENT.json
  verify.py --self-test

run: every case file in DIR (*.json) goes through tools/harness.py twice, once on the
original function and once on its replacement, each in its own process; the results are
written to OUT (default DIR/results) and compared. --boot defaults to $BB_BOOT, --lib to
$BB_GAME_LIB or build/game/libbbgame.so.

Exit codes: 0 all cases match, 1 mismatch, 3 bad input.
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import subprocess
import sys

SCHEMA = 1
RETURN_REGS = ('rax', 'rdx', 'xmm0', 'xmm1')
CALLEE_SAVED = ('rbx', 'rbp', 'rsp', 'r12', 'r13', 'r14', 'r15')
MAX_REPORTED = 8


class BadInput(Exception):
    pass


def split_addr(addr: str) -> tuple[str, int]:
    """'0x1234' -> ('', 0x1234); 'buf:out+0x4' -> ('buf:out', 4)."""
    if addr.startswith('buf:'):
        name, _, off = addr.partition('+')
        return name, int(off, 16) if off else 0
    return '', int(addr, 16)


def byte_map(writes: list[dict]) -> dict[tuple[str, int], int]:
    """Final value of every byte written; a later write replaces an earlier one."""
    m: dict[tuple[str, int], int] = {}
    for w in writes:
        try:
            base, addr = split_addr(w['addr'])
            data = bytes.fromhex(w['bytes'])
        except (KeyError, ValueError) as e:
            raise BadInput(f'bad write record {w!r}') from e
        for i, b in enumerate(data):
            m[(base, addr + i)] = b
    return m


def compare_case(orig: dict, repl: dict) -> list[str]:
    diffs: list[str] = []
    for side, d in (('original', orig), ('replacement', repl)):
        if 'fault' in d:
            diffs.append(f"{side}: {d['fault']}")
        for e in d.get('errors', []):
            diffs.append(f'{side}: {e}')
    if any('fault' in d for d in (orig, repl)):
        return diffs
    for group, regs in (('ret', RETURN_REGS), ('preserved', CALLEE_SAVED)):
        for r in regs:
            a = orig.get(group, {}).get(r)
            b = repl.get(group, {}).get(r)
            if a != b:
                diffs.append(f'{group}.{r}: original {a} replacement {b}')
    mo, mr = byte_map(orig.get('writes', [])), byte_map(repl.get('writes', []))
    bad = sorted(a for a in set(mo) | set(mr) if mo.get(a) != mr.get(a))
    for a in bad[:MAX_REPORTED]:
        o = f'{mo[a]:02x}' if a in mo else 'unwritten'
        r = f'{mr[a]:02x}' if a in mr else 'unwritten'
        diffs.append(f'memory {a[0]}+0x{a[1]:x}: original {o} replacement {r}')
    if len(bad) > MAX_REPORTED:
        diffs.append(f'memory: {len(bad) - MAX_REPORTED} more differing bytes')
    if orig.get('calls', []) != repl.get('calls', []):
        diffs.append('calls: external call sequence or arguments differ')
    return diffs


def compare(orig: dict, repl: dict) -> dict:
    for name, d in (('original', orig), ('replacement', repl)):
        if d.get('schema') != SCHEMA or 'cases' not in d:
            raise BadInput(f'{name}: not a schema {SCHEMA} result file')
    if orig.get('address') != repl.get('address'):
        raise BadInput('results are for different addresses')
    oc = {c['id']: c for c in orig['cases']}
    rc = {c['id']: c for c in repl['cases']}
    failures = []
    for cid in sorted(set(oc) | set(rc)):
        if cid not in oc or cid not in rc:
            failures.append({'case': cid, 'diffs': ['case missing from one side']})
            continue
        d = compare_case(oc[cid], rc[cid])
        if d:
            failures.append({'case': cid, 'diffs': d})
    n = len(set(oc) | set(rc))
    return {'schema': SCHEMA, 'address': orig.get('address'), 'cases': n,
            'passed': n - len(failures), 'failed': len(failures), 'failures': failures}


def load(path: str) -> dict:
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        raise BadInput(f'{path}: {e}') from e


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS = os.path.join(ROOT, 'tools', 'harness.py')
CASE_TIMEOUT = 20


def lookup(name: str) -> tuple[str, str]:
    """(address, replacement symbol) of a function, from symbols/ and game/hooks.csv."""
    with open(os.path.join(ROOT, 'symbols', 'functions.csv'), newline='') as f:
        rows = [r for r in csv.DictReader(f) if r['name'] == name]
    if not rows:
        raise BadInput(f'{name} is not in symbols/functions.csv')
    address = rows[0]['address']
    with open(os.path.join(ROOT, 'game', 'hooks.csv'), newline='') as f:
        hooks = [r for r in csv.DictReader(f) if r['address'] == address]
    if not hooks:
        raise BadInput(f'{name} has no replacement in game/hooks.csv')
    return address, hooks[0]['replacement']


def run_side(boot: str, case: str, address: str, symbol: str | None, lib: str | None) -> dict:
    cmd = [sys.executable, HARNESS, '--boot', boot, '--case', case, '--address', address]
    if symbol:
        cmd += ['--replacement', symbol, '--lib', lib]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=CASE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return {'fault': f'timeout after {CASE_TIMEOUT} s'}
    if r.returncode == 3:
        raise BadInput(f'{os.path.basename(case)}: {r.stderr.strip()}')
    if r.returncode != 0:
        how = f'signal {-r.returncode}' if r.returncode < 0 else f'exit {r.returncode}'
        return {'fault': f'{how}: {r.stderr.strip()[-300:]}'}
    return json.loads(r.stdout)


def run_cases(boot: str, captures: str, address: str, symbol: str, lib: str,
              patch: str | None = None, env: dict | None = None) -> dict:
    """Both sides of every case file in CAPTURES: {'original': [...], 'replacement': [...]}.
    One harness process per side maps the image once and forks per case."""
    cases = sorted(glob.glob(os.path.join(captures, '*.json')))
    if not cases:
        raise BadInput(f'no case files in {captures}')
    for path in cases:
        with open(path) as f:
            case = json.load(f)
        if case.get('address') != address:
            raise BadInput(f'{os.path.basename(path)} is for {case.get("address")}, not {address}')
    results: dict[str, list] = {}
    for side, sym in (('original', None), ('replacement', symbol)):
        cmd = [sys.executable, HARNESS, '--boot', boot, '--cases', captures, '--address', address,
               '--timeout', str(CASE_TIMEOUT)]
        side_env = None
        if sym:
            cmd += ['--replacement', sym, '--lib', lib]
            side_env = dict(os.environ, **(env or {}))
        if patch:
            # Both sides run on an image with a community patch applied (FILE:NAME): in the game an
            # option runs together with the patch, whose edits elsewhere (shared constants, other
            # functions) stay in place; its edits inside the replaced function never run.
            path, _, name = patch.partition(':')
            cmd += ['--patch', path, '--patch-name', name]
        r = subprocess.run(cmd, capture_output=True, text=True, env=side_env)
        if r.returncode != 0:
            raise BadInput(f'{side}: {r.stderr.strip()[-400:]}')
        by_file = json.loads(r.stdout)
        # The file name is the case's identity: ids inside files can repeat across runs.
        results[side] = [by_file[k] for k in sorted(by_file)]
    return results


def run(name: str, captures: str, boot: str | None, lib: str | None, out: str | None,
        patch: str | None = None, env: list | None = None) -> int:
    boot = boot or os.environ.get('BB_BOOT')
    lib = lib or os.environ.get('BB_GAME_LIB') or os.path.join(ROOT, 'build', 'game', 'libbbgame.so')
    if not boot or not os.path.isfile(boot):
        raise BadInput('no boot image: pass --boot or set BB_BOOT (bbport scripts/prepare.py output)')
    if not os.path.isfile(lib):
        raise BadInput(f'no game library at {lib}: build the repository or pass --lib')
    address, symbol = lookup(name)
    extra = dict(e.split('=', 1) for e in (env or []))
    results = run_cases(boot, captures, address, symbol, lib, patch, extra)
    out = out or os.path.join(captures, 'results')
    os.makedirs(out, exist_ok=True)
    files = {}
    for side, cases_out in results.items():
        doc = {'schema': SCHEMA, 'address': address, 'function': name, 'cases': cases_out}
        files[side] = os.path.join(out, f'{side}.json')
        with open(files[side], 'w') as f:
            json.dump(doc, f, indent=1, sort_keys=True)
    summary = compare(load(files['original']), load(files['replacement']))
    summary['function'] = name
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 1 if summary['failed'] else 0


def self_test() -> int:
    def case(cid, rax='0x1', writes=None, calls=None):
        return {'id': cid, 'ret': {'rax': rax}, 'preserved': {'rbx': '0x5'},
                'writes': writes or [], 'calls': calls or []}

    def res(*cases):
        return {'schema': SCHEMA, 'address': '0x00001000', 'cases': list(cases)}

    w1 = [{'addr': '0x100', 'bytes': '0102'}, {'addr': 'buf:out+0x0', 'bytes': '07'}]
    w1_split = [{'addr': '0x100', 'bytes': '01'}, {'addr': '0x101', 'bytes': '02'}, {'addr': 'buf:out+0x0', 'bytes': '07'}]
    w_over = [{'addr': '0x100', 'bytes': 'ff'}, {'addr': '0x100', 'bytes': '01'}, {'addr': '0x101', 'bytes': '02'},
              {'addr': 'buf:out', 'bytes': '07'}]
    ok = compare(res(case('a', writes=w1)), res(case('a', writes=w1_split)))
    ok2 = compare(res(case('a', writes=w1)), res(case('a', writes=w_over)))
    bad_ret = compare(res(case('a')), res(case('a', rax='0x2')))
    bad_mem = compare(res(case('a', writes=w1)), res(case('a', writes=[{'addr': '0x100', 'bytes': '0103'}])))
    bad_call = compare(res(case('a', calls=[{'target': '0x1', 'args': []}])), res(case('a')))
    missing = compare(res(case('a'), case('b')), res(case('a')))
    fault = compare(res(case('a')), res(dict(case('a'), fault='signal 11')))
    checks = [ok['failed'] == 0, ok2['failed'] == 0, bad_ret['failed'] == 1,
              bad_mem['failed'] == 1, bad_call['failed'] == 1, missing['failed'] == 1,
              fault['failed'] == 1]
    if not all(checks):
        print(f'verify: self-test failed {checks}', file=sys.stderr)
        return 1
    print('verify: self-test ok')
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--self-test', action='store_true')
    sub = ap.add_subparsers(dest='cmd')
    c = sub.add_parser('compare')
    c.add_argument('original')
    c.add_argument('replacement')
    r = sub.add_parser('run')
    r.add_argument('--function', required=True)
    r.add_argument('--captures', required=True)
    r.add_argument('--boot')
    r.add_argument('--lib')
    r.add_argument('--out')
    r.add_argument('--patch', metavar='FILE:NAME',
                   help='run both sides on an image with this community patch applied (checks an option against it)')
    r.add_argument('--env', action='append', metavar='KEY=VALUE', help='environment for the replacement side')
    a = ap.parse_args()

    if a.self_test:
        return self_test()
    if a.cmd == 'compare':
        try:
            out = compare(load(a.original), load(a.replacement))
        except BadInput as e:
            print(f'verify: {e}', file=sys.stderr)
            return 3
        print(json.dumps(out, indent=2, sort_keys=True))
        return 1 if out['failed'] else 0
    if a.cmd == 'run':
        try:
            return run(a.function, a.captures, a.boot, a.lib, a.out, a.patch, a.env)
        except (BadInput, OSError, json.JSONDecodeError) as e:
            print(f'verify: {e}', file=sys.stderr)
            return 3
    ap.print_help()
    return 3


if __name__ == '__main__':
    sys.exit(main())
