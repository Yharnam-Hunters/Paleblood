#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the session report a pull request carries (the PR template requires it).

usage: session_report.py [--base origin/main] [--verify RESULT.json ...] [--captures DIR ...]
       session_report.py --check-body FILE      (CI: the PR body has a valid report)

Compares symbols/functions.csv on this branch with the merge base: functions named (new rows),
replaced, edge-verified and verified (status changes). --verify takes the JSON that `tools/verify.py run`
prints (save it with `> result.json`), --captures the case directories used, counted only.
Findings and open questions come from commit messages on the branch: lines starting with
"Finding:" or "Question:". Prints Markdown to paste into the pull request, with a JSON block
that tools can read back. No local paths are written.
"""
import argparse
import csv
import glob
import io
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
START, END = '<!-- session-report:start -->', '<!-- session-report:end -->'
JSON_RE = re.compile(r'<!-- session-report-json\s*(\{.*?\})\s*-->', re.S)


def git(*args: str) -> str:
    return subprocess.run(['git', '-C', ROOT, *args], check=True, capture_output=True, text=True).stdout


def rows_of(text: str) -> dict:
    return {r['address']: r for r in csv.DictReader(io.StringIO(text))} if text.strip() else {}


def function_changes(before: dict, after: dict) -> dict:
    """Named (new rows), replaced and verified (status reached on this branch)."""
    out = {'named': [], 'replaced': [], 'edge-verified': [], 'verified': []}
    for addr, r in sorted(after.items()):
        old = before.get(addr)
        if old is None:
            out['named'].append({'address': addr, 'name': r['name'], 'system': r['system']})
        if r['status'] in ('replaced', 'edge-verified', 'verified') and (old is None or old['status'] == 'original'):
            out['replaced'].append({'address': addr, 'name': r['name']})
        if r['status'] == 'edge-verified' and (old is None or old['status'] in ('original', 'replaced')):
            out['edge-verified'].append({'address': addr, 'name': r['name']})
        if r['status'] == 'verified' and (old is None or old['status'] != 'verified'):
            out['verified'].append({'address': addr, 'name': r['name']})
    return out


def verify_results(paths: list) -> list:
    out = []
    for p in paths:
        with open(p) as f:
            d = json.load(f)
        out.append({'function': d.get('function', os.path.basename(p)), 'cases': d.get('cases', 0),
                    'passed': d.get('passed', 0), 'failed': d.get('failed', 0)})
    return out


def capture_counts(dirs: list) -> list:
    out = []
    for d in dirs:
        files = glob.glob(os.path.join(d, '*.json'))
        runs = {os.path.basename(f).rsplit('_', 1)[0] for f in files if '_' in os.path.basename(f)}
        out.append({'set': os.path.basename(os.path.normpath(d)), 'cases': len(files), 'runs': len(runs)})
    return out


def notes(base: str) -> tuple:
    log = git('log', '--format=%B%x00', f'{base}..HEAD')
    findings, questions = [], []
    for line in log.splitlines():
        s = line.strip()
        if s.lower().startswith('finding:'):
            findings.append(s[8:].strip())
        elif s.lower().startswith('question:'):
            questions.append(s[9:].strip())
    return findings, questions


def render(report: dict) -> str:
    f = report['functions']
    lines = [START, '### Session report', '',
             f"Branch `{report['branch']}`, {report['commits']} commit(s) on `{report['base']}`.", '']
    for key, title in (('named', 'Named'), ('replaced', 'Replaced'), ('edge-verified', 'Edge-verified'), ('verified', 'Verified')):
        items = f.get(key, [])
        lines.append(f'**{title}** ({len(items)}): ' +
                     (', '.join(f"`{i['name']}` (`{i['address']}`)" for i in items) if items else 'none'))
    lines.append('')
    if report['verify']:
        lines += ['| verify.py | Cases | Passed | Failed |', '|---|---:|---:|---:|']
        lines += [f"| `{v['function']}` | {v['cases']} | {v['passed']} | {v['failed']} |" for v in report['verify']]
        lines.append('')
    else:
        lines += ['verify.py: no results attached.', '']
    if report['captures']:
        lines.append('Captures used: ' + ', '.join(f"{c['set']} ({c['cases']} cases, {c['runs']} runs)"
                                                  for c in report['captures']))
        lines.append('')
    for key, title in (('findings', 'Findings'), ('questions', 'Open questions')):
        lines.append(f'**{title}**')
        lines += [f'- {x}' for x in report[key]] or ['- none']
        lines.append('')
    lines.append('<!-- session-report-json ' + json.dumps(report, sort_keys=True) + ' -->')
    lines.append(END)
    return '\n'.join(lines) + '\n'


def check_body(text: str) -> list:
    errs = []
    if START not in text or END not in text:
        return ['the pull request has no session report (run tools/session_report.py and paste its output)']
    m = JSON_RE.search(text)
    if not m:
        return ['the session report has no JSON block']
    try:
        d = json.loads(m.group(1))
    except json.JSONDecodeError as e:
        return [f'the session report JSON does not parse: {e}']
    for key in ('functions', 'verify', 'findings', 'questions'):
        if key not in d:
            errs.append(f'the session report has no "{key}"')
    for v in d.get('verify', []):
        if v.get('failed'):
            errs.append(f"verify.py reports {v['failed']} failing case(s) for {v.get('function')}")
    return errs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--base', default='origin/main')
    ap.add_argument('--verify', nargs='*', default=[])
    ap.add_argument('--captures', nargs='*', default=[])
    ap.add_argument('--check-body', metavar='FILE')
    a = ap.parse_args()
    if a.check_body:
        errs = check_body(open(a.check_body).read())
        for e in errs:
            print(f'session report: {e}', file=sys.stderr)
        return 1 if errs else 0
    base = git('merge-base', a.base, 'HEAD').strip()
    before = rows_of(git('show', f'{base}:symbols/functions.csv'))
    with open(os.path.join(ROOT, 'symbols', 'functions.csv')) as f:
        after = rows_of(f.read())
    findings, questions = notes(base)
    report = {'schema': 1, 'base': a.base, 'branch': git('rev-parse', '--abbrev-ref', 'HEAD').strip(),
              'commits': int(git('rev-list', '--count', f'{base}..HEAD').strip() or 0),
              'functions': function_changes(before, after), 'verify': verify_results(a.verify),
              'captures': capture_counts(a.captures), 'findings': findings, 'questions': questions}
    sys.stdout.write(render(report))
    return 0


if __name__ == '__main__':
    sys.exit(main())
