#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Where a refactor can land now: the open pull requests on each file, and quiet subsystems ranked.

usage: refactor_map.py [--root DIR] [--prs FILE] [--json] [--files] [--top N]

A cleanup pull request that rewrites a shared header while contributors have pull requests open on
the same files makes them all conflict, and (through verification rows) re-run. This map shows
which files and subsystems nobody is working on right now, and what in them is worth tidying.

For every C/C++ file under game/ and runtime/ it lists:
  prs        the open pull requests that touch it (`gh pr list --state open --json number,title,
             author,files`; `--prs FILE` reads that JSON instead, for offline use and tests)
  functions  the replacements it defines (bb_NAME, as in symbols/functions.csv), with how many are
             verified and how many rows they hold in symbols/verification.csv
  quiet      no open pull request touches it

A subsystem is a directory one level under game/ or runtime/ (game/ai, runtime/renderer). It is
quiet when no open pull request touches any file in it. Quiet subsystems are ranked as refactor
candidates by what a simple scan finds in them:
  duplicate structs  a struct whose member list is identical (comments and white space aside) to
                     one in another file, or a struct name defined in more than one file
  raw offsets        offset access STYLE.md forbids in new code (get<T>(p, N), ptr_at, p[0x..]) and
                     pointer arithmetic by a hex constant inside a cast
  unknown padding    `unknown_0x..` members: layout still to be named
  repeated code      runs of REPEAT_LINES consecutive code lines (comments and blank lines aside)
                     that appear in more than one file of the subsystem, or more than once in one
The score is functions + 4 x duplicate structs + raw offsets / 2 + repeated runs; a refactor of a
subsystem with many functions also re-records many rows, so the verification row count is shown
beside it (docs/VERIFY.md, "Source fingerprints": only functions that use what changed go stale).

The scan is a lead for a reviewer, not a finding: read the code before refactoring it, keep the
behaviour, and re-record what goes stale (CONTRIBUTING.md, "Refactoring while pull requests are
open").
"""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import check_readable  # noqa: E402

SOURCES = ('.c', '.cc', '.cpp', '.h', '.hpp', '.inl', '.mm')
TREES = ('game', 'runtime')
REPEAT_LINES = 6
MIN_REPEAT_CHARS = 120          # a run shorter than this is boilerplate (braces, returns)
MIN_STRUCT_MEMBERS = 2
REPLACEMENT = re.compile(r'\bbb_(\w+)\s*\([^;{]*\)\s*(?:const\s*)?\{')
STRUCT = re.compile(r'\b(?:struct|class)\s+(?:alignas\s*\([^)]*\)\s*)?(\w+)\s*(?:final\s*)?(?::[^{;]*)?\{')
OFFSET_ARITHMETIC = re.compile(r'\+\s*0[xX][0-9a-fA-F]+\s*\)')
UNKNOWN_PADDING = re.compile(r'\bunknown_\w+\s*\[')
OFFSET_RULES = [rule for label, rule in check_readable.RULES if label == 'offset access']


# ---- inputs ---------------------------------------------------------------------------------

def open_prs(root: str, prs_file: str | None) -> list[dict]:
    """[{number, title, author, files: [path]}] for every open pull request."""
    if prs_file:
        with open(prs_file) as f:
            data = json.load(f)
    else:
        out = subprocess.run(['gh', 'pr', 'list', '--state', 'open', '--limit', '500',
                              '--json', 'number,title,author,files'],
                             cwd=root, check=True, capture_output=True, text=True).stdout
        data = json.loads(out)
    prs = []
    for pr in data:
        files = [f['path'] if isinstance(f, dict) else f for f in pr.get('files') or []]
        if len(files) >= 100 and not prs_file:
            files = full_file_list(root, pr['number'])   # `gh pr list` stops at 100 files
        author = pr.get('author') or {}
        prs.append({'number': pr['number'], 'title': pr.get('title', ''),
                    'author': author.get('login', '') if isinstance(author, dict) else str(author),
                    'files': files})
    return prs


def full_file_list(root: str, number: int) -> list[str]:
    out = subprocess.run(['gh', 'pr', 'diff', str(number), '--name-only'],
                         cwd=root, check=True, capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line]


def source_files(root: str) -> list[str]:
    out = []
    for tree in TREES:
        for dp, dns, fns in os.walk(os.path.join(root, tree)):
            dns[:] = sorted(d for d in dns if d not in ('tests', '__pycache__'))
            for fn in sorted(fns):
                if fn.endswith(SOURCES):
                    out.append(os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, '/'))
    return sorted(out)


def read_rows(path: str) -> list[dict]:
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


def subsystem(rel: str) -> str:
    parts = rel.split('/')
    return '/'.join(parts[:2]) if len(parts) > 2 else parts[0]


# ---- the scan -------------------------------------------------------------------------------

def normalized_lines(code: str) -> list[tuple[int, str]]:
    """(line number, text with white space collapsed) of every non-blank code line."""
    out = []
    for n, line in enumerate(code.split('\n'), 1):
        text = ' '.join(line.split())
        if text and text not in ('{', '}', '};', '} // namespace', 'public:', 'private:'):
            out.append((n, text))
    return out


def struct_bodies(code: str) -> list[tuple[str, str, int]]:
    """(name, normalized member list, member count) for each struct or class with a body."""
    out = []
    for m in STRUCT.finditer(code):
        depth, i = 0, m.end() - 1
        while i < len(code):
            if code[i] == '{':
                depth += 1
            elif code[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = code[m.end():i]
        members = [' '.join(s.split()) for s in body.split(';') if s.strip()]
        out.append((m.group(1), '; '.join(members), len(members)))
    return out


def scan(root: str, files: list[str]) -> dict[str, dict]:
    """Per file: replacements, offset findings, padding, structs, and line runs for repeats."""
    out = {}
    for rel in files:
        with open(os.path.join(root, rel), errors='replace') as f:
            text = f.read()
        code, _ = check_readable.strip(text)
        # padding members (`uint8_t unknown_0x10[0x20];`) are counted apart, not as offset access
        body = '\n'.join(line for line in code.split('\n') if not check_readable.PADDING.match(line))
        offsets = sum(len(rule.findall(body)) for rule in OFFSET_RULES) + len(OFFSET_ARITHMETIC.findall(body))
        lines = normalized_lines(code)
        runs = []
        for k in range(len(lines) - REPEAT_LINES + 1):
            chunk = '\n'.join(t for _, t in lines[k:k + REPEAT_LINES])
            if len(chunk) >= MIN_REPEAT_CHARS:
                runs.append((hashlib.sha1(chunk.encode()).hexdigest(), lines[k][0]))
        out[rel] = {'replacements': sorted(set(REPLACEMENT.findall(code))),
                    'offsets': offsets,
                    'padding': len(UNKNOWN_PADDING.findall(code)),
                    'structs': [s for s in struct_bodies(code) if s[2] >= MIN_STRUCT_MEMBERS],
                    'runs': runs}
    return out


def duplicate_structs(scanned: dict[str, dict]) -> list[dict]:
    """Structs with the same member list in different files, and struct names defined in several."""
    by_body: dict[str, list] = collections.defaultdict(list)
    by_name: dict[str, list] = collections.defaultdict(list)
    for rel, info in scanned.items():
        for name, body, _ in info['structs']:
            by_body[body].append((rel, name))
            by_name[name].append((rel, body))
    groups = []
    for body, where in by_body.items():
        if len({rel for rel, _ in where}) > 1:
            groups.append({'kind': 'same members', 'structs': sorted(f'{rel}:{name}' for rel, name in where)})
    for name, where in by_name.items():
        if len({rel for rel, _ in where}) > 1 and len({body for _, body in where}) > 1:
            groups.append({'kind': 'same name', 'structs': sorted(f'{rel}:{name}' for rel, _ in where)})
    return sorted(groups, key=lambda g: g['structs'])


def repeated_runs(scanned: dict[str, dict], files: list[str]) -> list[dict]:
    """Runs of REPEAT_LINES code lines found more than once among `files`; overlapping windows of
    one repeated block count once (the first line of each block)."""
    seen: dict[str, list] = collections.defaultdict(list)
    for rel in files:
        for digest, line in scanned[rel]['runs']:
            seen[digest].append((rel, line))
    places = {d: w for d, w in seen.items() if len(w) > 1}
    blocks = []
    covered: set[tuple[str, int]] = set()
    for digest, where in sorted(places.items(), key=lambda kv: kv[1]):
        if all((rel, line) in covered for rel, line in where):
            continue
        blocks.append({'places': [f'{rel}:{line}' for rel, line in where]})
        for rel, line in where:
            for k in range(line, line + REPEAT_LINES * 3):
                covered.add((rel, k))
    return blocks


# ---- the map --------------------------------------------------------------------------------

def build(root: str, prs: list[dict]) -> dict:
    files = source_files(root)
    functions = {r['name']: r for r in read_rows(os.path.join(root, 'symbols', 'functions.csv'))}
    rows = collections.Counter(r['function'] for r in read_rows(os.path.join(root, 'symbols', 'verification.csv')))
    touching: dict[str, list[int]] = collections.defaultdict(list)
    for pr in prs:
        for path in pr['files']:
            touching[path].append(pr['number'])
    scanned = scan(root, files)
    file_rows = []
    for rel in files:
        info = scanned[rel]
        named = [n for n in info['replacements'] if n in functions]
        file_rows.append({'file': rel, 'subsystem': subsystem(rel), 'prs': sorted(touching.get(rel, [])),
                          'functions': len(named),
                          'verified': sum(1 for n in named if functions[n]['status'] == 'verified'),
                          'rows': sum(rows[n] for n in named), 'offsets': info['offsets'],
                          'padding': info['padding'], 'quiet': not touching.get(rel)})
    structs = duplicate_structs(scanned)
    systems: dict[str, dict] = {}
    for fr in file_rows:
        s = systems.setdefault(fr['subsystem'], {'subsystem': fr['subsystem'], 'files': 0, 'functions': 0,
                                                 'verified': 0, 'rows': 0, 'offsets': 0, 'padding': 0,
                                                 'prs': set(), 'duplicate_structs': 0, 'repeated_runs': 0,
                                                 'examples': []})
        s['files'] += 1
        for k in ('functions', 'verified', 'rows', 'offsets', 'padding'):
            s[k] += fr[k]
        s['prs'].update(fr['prs'])
    # PRs touching a subsystem's directory without touching a scanned file (a new file) count too
    for pr in prs:
        for path in pr['files']:
            if path.split('/')[0] in TREES and subsystem(path) in systems:
                systems[subsystem(path)]['prs'].add(pr['number'])
    for g in structs:
        for name in {subsystem(x.split(':')[0]) for x in g['structs']}:
            systems[name]['duplicate_structs'] += 1
            if len(systems[name]['examples']) < 3:
                systems[name]['examples'].append(f"{g['kind']}: " + ', '.join(g['structs'][:3]))
    for name, s in systems.items():
        members = [fr['file'] for fr in file_rows if fr['subsystem'] == name]
        blocks = repeated_runs(scanned, members)
        s['repeated_runs'] = len(blocks)
        for b in blocks[:max(0, 3 - len(s['examples']))]:
            s['examples'].append('repeated: ' + ', '.join(b['places'][:3]))
    ranked = []
    for s in systems.values():
        s['prs'] = sorted(s['prs'])
        s['quiet'] = not s['prs']
        s['score'] = round(s['functions'] + 4 * s['duplicate_structs'] + s['offsets'] / 2 + s['repeated_runs'], 1)
        ranked.append(s)
    ranked.sort(key=lambda s: (-s['score'], s['subsystem']))
    return {'files': file_rows, 'subsystems': ranked, 'duplicate_structs': structs,
            'prs': [{k: pr[k] for k in ('number', 'title', 'author')} for pr in prs]}


def report(m: dict, top: int, with_files: bool) -> str:
    out = []
    quiet = [s for s in m['subsystems'] if s['quiet'] and s['score'] > 0]
    busy = [s for s in m['subsystems'] if not s['quiet']]
    out.append(f"{len(m['prs'])} open pull requests; {len(quiet)} quiet subsystems with something to tidy, "
               f"{len(busy)} with open work.")
    out.append('')
    out.append('Quiet subsystems, best refactor candidates first:')
    out.append(f"{'score':>6} {'subsystem':<28} {'fns':>4} {'ver':>4} {'rows':>5} {'dup':>4} "
               f"{'offs':>5} {'pad':>4} {'rep':>4}")
    for s in quiet[:top]:
        out.append(f"{s['score']:>6} {s['subsystem']:<28} {s['functions']:>4} {s['verified']:>4} {s['rows']:>5} "
                   f"{s['duplicate_structs']:>4} {s['offsets']:>5} {s['padding']:>4} {s['repeated_runs']:>4}")
        for e in s['examples']:
            out.append(f"{'':>7}- {e}")
    out.append('')
    out.append('Subsystems with open pull requests (refactor after they merge):')
    for s in busy:
        out.append(f"  {s['subsystem']:<28} PRs {', '.join('#' + str(n) for n in s['prs'][:8])}"
                   + (' ...' if len(s['prs']) > 8 else ''))
    if with_files:
        out.append('')
        out.append('Files:')
        for fr in m['files']:
            flag = 'quiet' if fr['quiet'] else 'PRs ' + ', '.join('#' + str(n) for n in fr['prs'])
            out.append(f"  {fr['file']}: {fr['functions']} functions ({fr['verified']} verified, "
                       f"{fr['rows']} rows), {fr['offsets']} offsets, {fr['padding']} padding; {flag}")
    return '\n'.join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--prs', help='JSON of open pull requests (as gh pr list --json number,title,author,files)')
    ap.add_argument('--json', action='store_true', help='print the whole map as JSON')
    ap.add_argument('--files', action='store_true', help='list every file too')
    ap.add_argument('--top', type=int, default=15)
    a = ap.parse_args()
    m = build(a.root, open_prs(a.root, a.prs))
    if a.json:
        print(json.dumps(m, indent=1))
    else:
        print(report(m, a.top, a.files))
    return 0


if __name__ == '__main__':
    sys.exit(main())
