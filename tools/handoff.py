#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Hand a task over between sessions: by hand, Codex or Claude Code (CONTRIBUTING.md).

usage: handoff.py write --tool {manual,codex,claude} --next TEXT [--task TEXT]
                        [--ownership {claimed,unclaimed,not-required,unknown}] [--claim URL]
                        [--recordings {genuine,synthetic,none}] [--verify RESULT.json ...]
                        [--test "COMMAND: RESULT" ...] [--blocker TEXT ...] [--base origin/main]
       handoff.py show [--markdown]
       handoff.py check [FILE]

The next session, whoever runs it, starts from this file and the repository alone: no chat
history is shared. `write` reads the branch's state from git (repository, commit, branch,
functions changed against the merge base, uncommitted paths) and saves
.handoff/handoff.json and .handoff/HANDOFF.md, both git-ignored; the previous handoff moves into
`history`. Task, ownership, claim and recordings carry over unless given again; tests, blockers
and the next action are this session's own. `write` and `check` refuse local paths, secrets and
session links, so the Markdown can be pasted into an issue or pull request.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import session_report  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, '.handoff')
JSON_PATH, MD_PATH = os.path.join(DIR, 'handoff.json'), os.path.join(DIR, 'HANDOFF.md')
SCHEMA = 1
TOOLS = ('manual', 'codex', 'claude')
OWNERSHIP = ('claimed', 'unclaimed', 'not-required', 'unknown')
RECORDINGS = ('genuine', 'synthetic', 'none')
FORBIDDEN = (
    ('a local path', re.compile(r'(?<![\w.])(?:/home/|/Users/|/mnt/|/tmp/|/root/|/var/|~/)|\b[A-Za-z]:\\')),
    ('a secret-looking token', re.compile(
        r'\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{10,}|\bgithub_pat_[A-Za-z0-9_]{10,}|\bsk-[A-Za-z0-9_-]{16,}'
        r'|\bAKIA[0-9A-Z]{16}\b|-----BEGIN [A-Z ]*PRIVATE KEY')),
    ('a session link', re.compile(r'claude\.ai/code/session|Claude-Session:|chatgpt\.com/codex/tasks', re.I)),
)


def git(*args: str) -> str:
    return subprocess.run(['git', '-C', ROOT, *args], check=True, capture_output=True, text=True).stdout


def repository_slug() -> str:
    """owner/repo from the origin URL (https or ssh); never the local path."""
    try:
        url = git('remote', 'get-url', 'origin').strip()
    except subprocess.CalledProcessError:
        return 'unknown'
    m = re.search(r'github\.com[:/]+([\w.-]+/[\w.-]+?)(?:\.git)?/?$', url)
    return m.group(1) if m else 'unknown'


def changed_paths() -> list:
    lines = git('status', '--porcelain', '--untracked-files=all').splitlines()
    return [ln[3:] for ln in lines if not ln[3:].startswith('.handoff/')]


def functions_changed(base: str) -> dict:
    try:
        merge_base = git('merge-base', base, 'HEAD').strip()
        before = session_report.rows_of(git('show', f'{merge_base}:symbols/functions.csv'))
    except subprocess.CalledProcessError:
        return {}
    with open(os.path.join(ROOT, 'symbols', 'functions.csv')) as f:
        after = session_report.rows_of(f.read())
    return session_report.function_changes(before, after)


def load(path: str = JSON_PATH) -> dict | None:
    if not os.path.isfile(path):
        return None
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for k, v in value.items():
            yield k
            yield from strings(v)
    elif isinstance(value, list):
        for v in value:
            yield from strings(v)


def check(d) -> list:
    if not isinstance(d, dict):
        return ['the handoff must be a JSON object']
    errs = []
    if d.get('schema') != SCHEMA:
        errs.append(f'"schema" must be {SCHEMA}')
    for key in ('repository', 'commit', 'branch', 'worktree', 'task', 'tool', 'next_action', 'written_at'):
        if not isinstance(d.get(key), str) or not d[key].strip():
            errs.append(f'"{key}" must be a non-empty string')
    if isinstance(d.get('commit'), str) and not re.fullmatch(r'[0-9a-f]{40}', d['commit']):
        errs.append('"commit" must be a full 40-character commit hash')
    if d.get('tool') not in TOOLS:
        errs.append(f'"tool" must be one of {", ".join(TOOLS)}')
    if isinstance(d.get('worktree'), str) and re.search(r'[/\\]', d['worktree']):
        errs.append('"worktree" is the directory name only')
    own = d.get('ownership')
    if not isinstance(own, dict) or own.get('state') not in OWNERSHIP:
        errs.append(f'"ownership.state" must be one of {", ".join(OWNERSHIP)}')
    elif own.get('claim') and not str(own['claim']).startswith('https://github.com/'):
        errs.append('"ownership.claim" must be a GitHub link (the claim comment or issue)')
    ev = d.get('evidence')
    if not isinstance(ev, dict) or ev.get('recordings') not in RECORDINGS:
        errs.append(f'"evidence.recordings" must be one of {", ".join(RECORDINGS)}')
    elif not isinstance(ev.get('functions'), dict) or not isinstance(ev.get('verify'), list):
        errs.append('"evidence" needs "functions" (object) and "verify" (list)')
    for key in ('tests', 'blockers', 'history'):
        if not isinstance(d.get(key), list):
            errs.append(f'"{key}" must be a list')
    if not all(isinstance(t, str) and ': ' in t for t in d.get('tests') or []):
        errs.append('each test is "COMMAND: RESULT"')
    dirty = d.get('dirty')
    if not isinstance(dirty, dict) or type(dirty.get('paths')) is not int or dirty['paths'] < 0:
        errs.append('"dirty.paths" must be a non-negative integer')
    for text in strings(d):
        for what, pattern in FORBIDDEN:
            m = pattern.search(text)
            if m:
                errs.append(f'contains {what} ({m.group(0)!r}); handoffs are shared, keep it out')
    return errs


def render(d: dict) -> str:
    fn = d['evidence']['functions']
    changed = ', '.join(f"{group} {len(items)}" for group, items in fn.items() if items) or 'none'
    own = d['ownership']
    lines = ['## Handoff', '',
             f"- **Repository:** `{d['repository']}` at `{d['commit']}`",
             f"- **Branch / worktree:** `{d['branch']}` / `{d['worktree']}`"
             + (f" ({d['dirty']['paths']} uncommitted path(s))" if d['dirty']['paths'] else ' (clean)'),
             f"- **Task:** {d['task']}",
             f"- **Written by:** {d['tool']}, {d['written_at']}",
             f"- **Ownership:** {own['state']}" + (f" ({own['claim']})" if own.get('claim') else ''),
             f"- **Recordings:** {d['evidence']['recordings']}; functions changed: {changed}"]
    lines += [f"- **verify.py:** `{v['function']}` {v['passed']}/{v['cases']} passed, {v['failed']} failed"
              for v in d['evidence']['verify']]
    lines += ['', '**Tests**'] + ([f'- `{t}`' for t in d['tests']] or ['- none run'])
    lines += ['', '**Blockers**'] + ([f'- {b}' for b in d['blockers']] or ['- none'])
    lines += ['', f"**Next action:** {d['next_action']}"]
    if d['history']:
        lines += ['', '**Earlier sessions:** ' + '; '.join(
            f"{h['tool']} at `{h['commit'][:12]}` ({h['written_at']})" for h in d['history'])]
    return '\n'.join(lines) + '\n'


def atomic_write(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix='.handoff-')
    with os.fdopen(fd, 'w', encoding='utf-8') as f:
        f.write(text)
    os.replace(tmp, path)


def write(a) -> int:
    previous = load()
    keep = previous or {}
    paths = changed_paths()
    d = {
        'schema': SCHEMA,
        'repository': repository_slug(),
        'commit': git('rev-parse', 'HEAD').strip(),
        'branch': git('rev-parse', '--abbrev-ref', 'HEAD').strip(),
        'worktree': os.path.basename(ROOT),
        'task': a.task or keep.get('task', ''),
        'tool': a.tool,
        'ownership': {'state': a.ownership or keep.get('ownership', {}).get('state', 'unknown'),
                      'claim': a.claim if a.claim is not None else keep.get('ownership', {}).get('claim', '')},
        'evidence': {'functions': functions_changed(a.base),
                     'verify': session_report.verify_results(a.verify),
                     'recordings': a.recordings or keep.get('evidence', {}).get('recordings', 'none')},
        'tests': a.test,
        'blockers': a.blocker,
        'next_action': a.next,
        'dirty': {'dirty': bool(paths), 'paths': len(paths)},
        'written_at': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'history': keep.get('history', []) + ([{k: previous[k] for k in ('tool', 'commit', 'written_at', 'next_action')}]
                                              if previous else []),
    }
    errs = check(d)
    if errs:
        for e in errs:
            print(f'handoff: {e}', file=sys.stderr)
        return 1
    atomic_write(JSON_PATH, json.dumps(d, indent=2, sort_keys=True) + '\n')
    atomic_write(MD_PATH, render(d))
    print(f'handoff: wrote .handoff/handoff.json and .handoff/HANDOFF.md ({len(d["history"])} earlier session(s))')
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='command', required=True)
    w = sub.add_parser('write')
    w.add_argument('--tool', required=True, choices=TOOLS)
    w.add_argument('--next', required=True, help='the next concrete action')
    w.add_argument('--task')
    w.add_argument('--ownership', choices=OWNERSHIP)
    w.add_argument('--claim', help='link to the claim (issue comment or claim issue)')
    w.add_argument('--recordings', choices=RECORDINGS)
    w.add_argument('--verify', nargs='*', default=[], help='JSON printed by tools/verify.py run')
    w.add_argument('--test', action='append', default=[], help='"COMMAND: RESULT", repeatable')
    w.add_argument('--blocker', action='append', default=[])
    w.add_argument('--base', default='origin/main')
    s = sub.add_parser('show')
    s.add_argument('--markdown', action='store_true')
    c = sub.add_parser('check')
    c.add_argument('file', nargs='?', default=JSON_PATH)
    a = ap.parse_args()
    if a.command == 'write':
        if not a.task and not (load() or {}).get('task'):
            ap.error('--task is required for the first handoff on this worktree')
        return write(a)
    try:
        d = load(a.file if a.command == 'check' else JSON_PATH)
    except json.JSONDecodeError as e:
        print(f'handoff: the handoff does not parse: {e}', file=sys.stderr)
        return 1
    if d is None:
        print('handoff: no handoff yet (tools/handoff.py write)', file=sys.stderr)
        return 1
    errs = check(d)
    for e in errs:
        print(f'handoff: {e}', file=sys.stderr)
    if a.command == 'show' and not errs:
        sys.stdout.write(render(d) if a.markdown else json.dumps(d, indent=2, sort_keys=True) + '\n')
    elif a.command == 'check' and not errs:
        print('handoff: ok')
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
