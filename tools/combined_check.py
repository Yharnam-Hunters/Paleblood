#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a pull request together with each other open pull request.

usage: combined_check.py [--base REF] [--overlap] [--max-pairs N] [--check CMD]... [--summary FILE]
                         LABEL=REF...

Starts from the current checkout (in CI, the pull request already merged into main). For each
LABEL=REF it merges REF on top, runs every check, and resets. Two pull requests can each pass on
their own and still break together (the same name defined twice, a call to a function the other
renamed); this finds that before either is queued. A textual conflict is only noted: the second
one to merge resolves it. When a pair fails and --base is given, the other pull request is checked
alone on the base; if it fails there too, it is broken on its own and the pair is not blamed.
Exits 1 when a pair merges cleanly and fails together only.

With --overlap (needs --base) only the pull requests that change a file this one changes, or a file
in a directory this one changes (the same source directory, so usually the same CMake target), are
checked; the others cannot break together by name or build and are listed as skipped. --max-pairs
keeps the closest N of those: most shared files first, then most shared directories, then in the
order given (oldest pull request first in CI). The build directory is kept between pairs, so after
the first pair each build is incremental.
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


def changed_files(base: str, ref: str) -> set[str]:
    """The files `ref` changes since it left `base` (empty when git cannot tell)."""
    result = git('diff', '--name-only', f'{base}...{ref}')
    return set(result.stdout.split()) if result.returncode == 0 else set()


def directories(paths: set[str]) -> set[str]:
    """The directories holding `paths`; the top level is left out, since every build shares it."""
    return {path.rpartition('/')[0] for path in paths} - {''}


def overlap(own: set[str], other: set[str]) -> tuple[int, int]:
    """(shared files, shared directories) between two changed-file sets."""
    return len(own & other), len(directories(own) & directories(other))


def select_pairs(own: set[str], others: list[tuple[str, set[str]]],
                 limit: int | None) -> tuple[list[str], list[str]]:
    """(chosen, skipped) refs: those sharing a file or directory with `own`, closest first, at most
    `limit` of them (None for all). Ties keep the given order."""
    scored = [(overlap(own, files), index, ref) for index, (ref, files) in enumerate(others)]
    related = sorted((s for s in scored if s[0] != (0, 0)), key=lambda s: (-s[0][0], -s[0][1], s[1]))
    chosen = {ref for _, _, ref in related[:limit]}
    return ([ref for ref, _ in others if ref in chosen],
            [ref for ref, _ in others if ref not in chosen])


def skip_reason(by_overlap: bool, limit: int | None) -> str:
    """Why the report leaves some pull requests out."""
    if not by_overlap:
        return f'beyond the first {limit}'
    if limit is None:
        return 'no changed file or directory in common'
    return f'no changed file or directory in common, or beyond the closest {limit}'


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
    parser.add_argument('--overlap', action='store_true',
                        help='check only the pull requests that share a changed file or directory with this one (needs --base)')
    parser.add_argument('--max-pairs', type=int, metavar='N', help='check at most the N closest pull requests')
    parser.add_argument('others', nargs='*', metavar='LABEL=REF')
    args = parser.parse_args()
    checks = args.check or CHECKS
    if args.overlap and not args.base:
        parser.error('--overlap needs --base')
    if args.max_pairs is not None and args.max_pairs < 0:
        parser.error('--max-pairs must not be negative')

    if git('status', '--porcelain', '--untracked-files=no').stdout.strip():
        print('combined: the checkout has uncommitted changes', file=sys.stderr)
        return 2
    start = git('rev-parse', 'HEAD').stdout.strip()
    branch = git('symbolic-ref', '-q', '--short', 'HEAD').stdout.strip()
    labels = {}
    for other in args.others:
        label, sep, ref = other.partition('=')
        if not sep:
            label = ref = other
        labels[ref] = label
    refs, skipped = list(labels), []
    if args.overlap:
        own = changed_files(args.base, start)
        refs, skipped = select_pairs(own, [(ref, changed_files(args.base, ref)) for ref in refs], args.max_pairs)
    elif args.max_pairs is not None:
        refs, skipped = refs[:args.max_pairs], refs[args.max_pairs:]
    rows, details, broken = [], [], False
    for ref in refs:
        label = labels[ref]
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
    elif not skipped:
        report += 'No other open pull requests.\n'
    if skipped:
        report += f'\nNot checked together ({skip_reason(args.overlap, args.max_pairs)}): '
        report += ', '.join(labels[ref] for ref in skipped) + '\n'
    if args.summary:
        with open(args.summary, 'a', encoding='utf-8') as out:
            out.write(report)
    else:
        print(report)
    git('checkout', '-q', *([branch] if branch else ['--detach', start]))
    return 1 if broken else 0


if __name__ == '__main__':
    sys.exit(main())
