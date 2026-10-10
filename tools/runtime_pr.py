#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Select and validate runtime, documentation, or full pull request reports from changed files.

usage: runtime_pr.py --check-pull-request --base BASE --head HEAD --body FILE

Runtime-only and documentation-only reports are available only for explicit changed-file scopes.
All other changes use the full session report. Repository build, tests, policy and readable-code
checks still run for every pull request.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import PurePosixPath

import session_report

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RUNTIME_TEST_FILES = {'test/test_syslib.c', 'test/test_plugin.c'}
RUNTIME_CODE_SUFFIXES = {'.c', '.cc', '.cpp', '.h', '.hh', '.hpp', '.s', '.S'}
RUNTIME_DOC_FILES = {
    'docs/runtime.md',
    'docs/runtime-syscalls.md',
    'docs/systems/frame_timing.md',
    'docs/systems/kernel.md',
}
RUNTIME_CHANGE_FILES = {
    'CMakeLists.txt',
    'runtime/CMakeLists.txt',
    'test/CMakeLists.txt',
    'test/test_syslib.c',
    *RUNTIME_DOC_FILES,
}


def safe_repo_path(path: str) -> bool:
    p = PurePosixPath(path)
    return bool(path) and not p.is_absolute() and '..' not in p.parts and '\\' not in path


def is_runtime_code_path(path: str) -> bool:
    if path in RUNTIME_TEST_FILES:
        return True
    p = PurePosixPath(path)
    return bool(p.parts) and p.parts[0] == 'runtime' and (
        p.suffix in RUNTIME_CODE_SUFFIXES or p.name in {'CMakeLists.txt', 'Kbuild'}
    )


def is_runtime_document_path(path: str) -> bool:
    return path in RUNTIME_DOC_FILES or (
        path.startswith('docs/runtime/') and PurePosixPath(path).suffix.lower() == '.md'
    )


def is_documentation_path(path: str) -> bool:
    p = PurePosixPath(path)
    return p.suffix.lower() == '.md' and (
        len(p.parts) == 1 or p.parts[0] == 'docs' or p.parts[0] == '.github'
    )


def is_runtime_change_path(path: str) -> bool:
    return is_runtime_code_path(path) or is_runtime_document_path(path) or path in RUNTIME_CHANGE_FILES


def classify_paths(paths: list[str]) -> dict[str, str | bool]:
    """Fail closed to the full report; labels and PR-body claims do not select a path."""
    safe = all(safe_repo_path(p) for p in paths)
    runtime_changed = any(
        is_runtime_code_path(p) or is_runtime_document_path(p) or p in RUNTIME_CHANGE_FILES
        for p in paths
    )
    runtime_only = bool(paths) and safe and any(is_runtime_code_path(p) for p in paths) \
        and all(is_runtime_change_path(p) for p in paths)
    documentation_only = bool(paths) and safe and all(is_documentation_path(p) for p in paths)
    if runtime_only:
        classification = 'runtime-only'
    elif documentation_only:
        classification = 'documentation-only'
    else:
        classification = 'decompilation/mixed'
    return {'classification': classification, 'runtime_changed': runtime_changed}


def git(*args: str) -> str:
    return subprocess.run(['git', '-C', ROOT, *args], check=True, capture_output=True, text=True).stdout.strip()


def changed_paths(base: str, head: str) -> tuple[str, list[str]]:
    merge_base = git('merge-base', base, head)
    raw = subprocess.run(
        ['git', '-C', ROOT, 'diff', '--no-renames', '--name-only', '-z', merge_base, head],
        check=True, capture_output=True,
    ).stdout
    return merge_base, [p.decode('utf-8', 'surrogateescape') for p in raw.split(b'\0') if p]


def changed_replacements(base: str, head: str) -> list[dict]:
    before = session_report.rows_of(git('show', f'{base}:symbols/functions.csv'))
    after = session_report.rows_of(git('show', f'{head}:symbols/functions.csv'))
    changes = session_report.function_changes(before, after)
    replacements = {}
    for group in ('replaced', 'edge-verified', 'verified'):
        for item in changes[group]:
            replacements[item['name']] = item
    return [replacements[name] for name in sorted(replacements)]


def check_runtime_report(text: str) -> list[str]:
    errors = []
    start, end = '<!-- runtime-report:start -->', '<!-- runtime-report:end -->'
    marker = '<!-- runtime-report-json '
    if text.count(start) != 1 or text.count(end) != 1:
        return ['runtime-only PR needs the concise runtime report block from the PR template']
    block_start, block_end = text.index(start), text.index(end)
    if block_start >= block_end or text.count(marker) != 1:
        return ['runtime-only report needs one JSON block inside its report markers']
    json_start = text.index(marker)
    if not block_start < json_start < block_end:
        return ['runtime-only report has no runtime-report-json block']
    json_end = text.find('-->', json_start)
    if json_end < 0 or json_end > block_end:
        return ['runtime-only report JSON block is not closed inside its report markers']
    raw = text[json_start + len(marker):json_end].strip()
    try:
        report = json.loads(raw)
    except (json.JSONDecodeError, IndexError) as exc:
        return [f'runtime-only report JSON does not parse: {exc}']
    if not isinstance(report, dict):
        return ['runtime-only report JSON must be an object']
    if type(report.get('schema')) is not int or report.get('schema') != 1 \
            or report.get('track') != 'runtime':
        errors.append('runtime-only report needs schema 1 and track "runtime"')
    report_lists = ('calls', 'sources', 'confirmed', 'unresolved', 'tests')
    for key in report_lists:
        if not isinstance(report.get(key), list):
            errors.append(f'runtime-only report needs a "{key}" list')
    for key in ('calls', 'sources', 'confirmed', 'tests'):
        if isinstance(report.get(key), list) and not report[key]:
            errors.append(f'runtime-only report "{key}" list must not be empty')
    for key in report_lists:
        if isinstance(report.get(key), list) and any(not isinstance(item, str) or not item.strip()
                                                     or '<' in item or 'todo' in item.lower()
                                                     or 'replace with' in item.lower()
                                                     or 'tbd' in item.lower()
                                                     for item in report[key]):
            errors.append(f'runtime-only report "{key}" list still has a placeholder or invalid item')
    if report.get('clean_room') is not True:
        errors.append('runtime-only report must affirm the clean-room requirement')
    if report.get('no_emulator_code') is not True:
        errors.append('runtime-only report must confirm that no emulator code was used')
    if not isinstance(report.get('clean_room_basis'), str) or not report['clean_room_basis'].strip() \
            or any(word in report['clean_room_basis'].lower() for word in ('replace with', 'todo', 'tbd')) \
            or '<' in report['clean_room_basis']:
        errors.append('runtime-only report must describe the clean-room basis')
    if report.get('game_data') is not False:
        errors.append('runtime-only report must state that no game data was used or included')
    if report.get('function_verify') != 'not-required':
        errors.append('function-level verify.py evidence is not required for runtime-only PRs')
    if report.get('maintainer_boot') != 'pending-after-merge':
        errors.append('runtime-only report must leave boot verification pending for the maintainer')
    return errors


def check_documentation_report(text: str) -> list[str]:
    errors = []
    start, end = '<!-- documentation-report:start -->', '<!-- documentation-report:end -->'
    marker = '<!-- documentation-report-json '
    if text.count(start) != 1 or text.count(end) != 1:
        return ['documentation-only PR needs the concise documentation report block from the PR template']
    block_start, block_end = text.index(start), text.index(end)
    if block_start >= block_end or text.count(marker) != 1:
        return ['documentation-only report needs one JSON block inside its report markers']
    json_start = text.index(marker)
    if not block_start < json_start < block_end:
        return ['documentation-only report has no documentation-report-json block']
    json_end = text.find('-->', json_start)
    if json_end < 0 or json_end > block_end:
        return ['documentation-only report JSON block is not closed inside its report markers']
    try:
        report = json.loads(text[json_start + len(marker):json_end].strip())
    except json.JSONDecodeError as exc:
        return [f'documentation-only report JSON does not parse: {exc}']
    if not isinstance(report, dict):
        return ['documentation-only report JSON must be an object']
    if type(report.get('schema')) is not int or report.get('schema') != 1 \
            or report.get('track') != 'documentation':
        errors.append('documentation-only report needs schema 1 and track "documentation"')
    pages = report.get('pages')
    if not isinstance(pages, list) or not pages or any(
        not isinstance(page, str) or not page.strip() or '<' in page
        or any(token in page.lower() for token in ('todo', 'tbd', 'replace with'))
        for page in pages
    ):
        errors.append('documentation-only report needs a non-empty list of completed page paths')
    summary = report.get('summary')
    if not isinstance(summary, str) or not summary.strip() or '<' in summary \
            or any(token in summary.lower() for token in ('todo', 'tbd', 'replace with')):
        errors.append('documentation-only report needs a concise summary without placeholders')
    if report.get('implementation_changes') is not False:
        errors.append('documentation-only report must confirm that implementation files did not change')
    return errors


def check_report(text: str, paths: list[str], required_replacements: list | None = None) -> list[str]:
    """Validate the report selected from changed files, never from labels or body claims."""
    scope = classify_paths(paths)
    if scope['classification'] == 'runtime-only':
        return check_runtime_report(text)
    if scope['classification'] == 'documentation-only':
        return check_documentation_report(text)
    return session_report.check_body(text, required_replacements)


def run_check(argv: list[str], label: str) -> bool:
    result = subprocess.run(argv, cwd=ROOT)
    if result.returncode:
        print(f'runtime-pr: {label} failed', file=sys.stderr)
        return False
    return True


def check_pull_request(base: str, head: str, body_path: str) -> int:
    try:
        merge_base, paths = changed_paths(base, head)
    except subprocess.CalledProcessError as exc:
        print(f'runtime-pr: cannot inspect PR changed files: {exc}', file=sys.stderr)
        return 2
    scope = classify_paths(paths)
    kind = scope['classification']
    print(f'runtime-pr: {kind} report path ({len(paths)} changed file(s))')

    if scope['runtime_changed']:
        checks = [
            (['git', '-C', ROOT, 'diff', '--check', merge_base, head], 'whitespace/format check'),
            ([sys.executable, os.path.join(ROOT, 'tools', 'check_no_game_data.py'), '--tracked'], 'repository hygiene check'),
            ([sys.executable, os.path.join(ROOT, 'tools', 'check_agnostic.py')], 'game-agnostic runtime check'),
        ]
        for argv, label in checks:
            if not run_check(argv, label):
                return 1

    try:
        body = open(body_path, encoding='utf-8').read()
    except OSError as exc:
        print(f'runtime-pr: cannot read pull request body: {exc}', file=sys.stderr)
        return 2

    required = None
    if scope['classification'] == 'decompilation/mixed':
        try:
            required = changed_replacements(merge_base, head)
        except (subprocess.CalledProcessError, KeyError) as exc:
            print(f'runtime-pr: cannot inspect changed function replacements: {exc}', file=sys.stderr)
            return 2
    errors = check_report(body, paths, required)
    for error in errors:
        print(f'runtime-pr: {error}', file=sys.stderr)
    return 1 if errors else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check-pull-request', action='store_true')
    ap.add_argument('--base')
    ap.add_argument('--head')
    ap.add_argument('--body')
    a = ap.parse_args()
    if not a.check_pull_request or not a.base or not a.head or not a.body:
        ap.error('--check-pull-request requires --base, --head and --body')
    return check_pull_request(a.base, a.head, a.body)


if __name__ == '__main__':
    sys.exit(main())
