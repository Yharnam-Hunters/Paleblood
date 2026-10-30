#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a pull request together with each other open pull request.

usage: combined_check.py [--base REF] [--check CMD]... [--summary FILE] LABEL=REF...

Starts from the current checkout (in CI, the pull request already merged into main). For each
LABEL=REF it merges REF on top, runs every check, and resets. Two pull requests can each pass on
their own and still break together (the same name defined twice, a call to a function the other
renamed); this finds that before either is queued. A textual conflict is only noted: the second
one to merge resolves it. When a pair fails and --base is given, the other pull request is checked
alone on the base; if it fails there too, it is broken on its own and the pair is not blamed.
Exits 1 when a pair merges cleanly and fails together only.
"""
from __future__ import annotations

import argparse
import subprocess
import sys

CHECKS = [
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release',
    'cmake --build build -j',
    'ctest --test-dir build --output-on-failure --no-tests=error',
    'python3 tools/validate_functions.py',
]
IDENTITY = ['-c', 'user.name=combined-check', '-c', 'user.email=combined-check@localhost']
TAIL_LINES = 30


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(['git', *IDENTITY, *args], capture_output=True, text=True)


def run_checks(checks: list[str]) -> tuple[str, str] | None:
    """The first failing check and the end of its output, or None when all pass."""
    for check in checks:
        result = subprocess.run(check, shell=True, capture_output=True, text=True)
        if result.returncode:
            tail = '\n'.join((result.stdout + result.stderr).splitlines()[-TAIL_LINES:])
            return check, tail
    return None


def check_pair(ref: str, start: str, checks: list[str]) -> tuple[str, str]:
    """(outcome, detail) for `start` with `ref` merged in; outcome is ok, conflict or broken."""
    git('checkout', '-q', '--detach', start)
    try:
        merge = git('merge', '--no-edit', '--no-ff', ref)
        if merge.returncode:
            conflicted = git('diff', '--name-only', '--diff-filter=U').stdout.split()
            git('merge', '--abort')
            return 'conflict', ', '.join(conflicted) or merge.stderr.strip()
        if git('rev-parse', 'HEAD^{tree}').stdout == git('rev-parse', start + '^{tree}').stdout:
            return 'ok', 'already included'
        failed = run_checks(checks)
        if failed:
            return 'broken', f'`{failed[0]}` fails:\n\n```\n{failed[1]}\n```'
        return 'ok', ''
    finally:
        git('reset', '--hard', '-q', start)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', help='the branch the pull requests merge into, to tell a pair that breaks from one already broken')
    parser.add_argument('--check', action='append', help='a shell command to run on each pair (default: build, ctest, validate_functions)')
    parser.add_argument('--summary', help='append the Markdown report to this file')
    parser.add_argument('others', nargs='*', metavar='LABEL=REF')
    args = parser.parse_args()
    checks = args.check or CHECKS

    if git('status', '--porcelain', '--untracked-files=no').stdout.strip():
        print('combined: the checkout has uncommitted changes', file=sys.stderr)
        return 2
    start = git('rev-parse', 'HEAD').stdout.strip()
    branch = git('symbolic-ref', '-q', '--short', 'HEAD').stdout.strip()
    rows, details, broken = [], [], False
    for other in args.others:
        label, sep, ref = other.partition('=')
        if not sep:
            label = ref = other
        outcome, detail = check_pair(ref, start, checks)
        if outcome == 'broken' and args.base and check_pair(ref, args.base, checks)[0] == 'broken':
            outcome = 'alone'
        print(f'combined: with {label}: {outcome}', flush=True)
        broken |= outcome == 'broken'
        if outcome == 'broken':
            rows.append(f'| {label} | breaks together |')
            details.append(f'#### With {label}\n\n{detail}')
        elif outcome == 'alone':
            rows.append(f'| {label} | fails on its own on the base too; not checked together |')
        elif outcome == 'conflict':
            rows.append(f'| {label} | text conflict in {detail}; the second to merge resolves it |')
        else:
            rows.append(f'| {label} | {detail or "builds and passes together"} |')

    report = '### Combined with the other open pull requests\n\n'
    if rows:
        report += '| Pull request | Result |\n|---|---|\n' + '\n'.join(rows) + '\n'
        report += ''.join('\n' + d + '\n' for d in details)
    else:
        report += 'No other open pull requests.\n'
    if args.summary:
        with open(args.summary, 'a', encoding='utf-8') as out:
            out.write(report)
    else:
        print(report)
    git('checkout', '-q', *([branch] if branch else ['--detach', start]))
    return 1 if broken else 0


if __name__ == '__main__':
    sys.exit(main())
