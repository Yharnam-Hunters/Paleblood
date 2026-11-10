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


def family_rows(text: str) -> dict:
    """address -> status from a symbols/families/<family>.csv text."""
    return {r['address']: r['status'] for r in csv.DictReader(io.StringIO(text))} if text.strip() else {}


def family_changes(families: dict, before: dict, after: dict) -> list:
    """Per family whose members changed status on this branch: how many became replaced,
    edge-verified and verified. FAMILIES: name -> its symbols/families.csv row; BEFORE, AFTER:
    name -> {address: status}."""
    out = []
    for name in sorted(after):
        old, new = before.get(name, {}), after[name]
        replaced = sum(1 for a, st in new.items() if st in HOOKED and old.get(a, 'original') == 'original')
        edge = sum(1 for a, st in new.items() if st == 'edge-verified' and old.get(a, 'original') in ('original', 'replaced'))
        verified = sum(1 for a, st in new.items() if st == 'verified' and old.get(a) != 'verified')
        if replaced or edge or verified:
            row = families.get(name, {})
            out.append({'family': name, 'shape': row.get('shape', ''), 'members': len(new),
                        'bytes': int(row.get('bytes', 0) or 0), 'replaced': replaced,
                        'edge-verified': edge, 'verified': verified})
    return out


def branch_family_changes(base: str, read_head) -> list:
    """family_changes between the merge base and the tree READ_HEAD(path) reads (None: absent)."""
    def at_base(path):
        try:
            return git('show', f'{base}:{path}')
        except subprocess.CalledProcessError:
            return ''
    head_table = read_head('symbols/families.csv') or ''
    families = {r['family']: r for r in csv.DictReader(io.StringIO(head_table))} if head_table.strip() else {}
    before, after = {}, {}
    for name in families:
        path = f'symbols/families/{name}.csv'
        before[name] = family_rows(at_base(path))
        after[name] = family_rows(read_head(path) or '')
    return family_changes(families, before, after)


HOOKED = ('replaced', 'edge-verified', 'verified')


def verify_results(paths: list) -> list:
    """Each path is a verify.py run summary (or a `verify.py family` summary), or the --out
    directory of a run, which holds one."""
    out = []
    for p in paths:
        if os.path.isdir(p):
            p = os.path.join(p, 'summary.json')
        with open(p) as f:
            d = json.load(f)
        if 'family' in d:
            out.append({'function': d.get('function', f"family:{d['family']}"), 'family': d['family'],
                        'members': d.get('members', 0), 'cases': d.get('cases', 0),
                        'passed': d.get('passed', 0), 'failed': d.get('failed', 0),
                        'mutants': [{k: m.get(k) for k in ('mutant', 'members', 'killed')}
                                    for m in d.get('mutants', [])]})
            continue
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
    if report.get('families'):
        lines += ['| Family | Shape | Members | Bytes | Newly replaced | Newly edge-verified | Newly verified |',
                  '|---|---|---:|---:|---:|---:|---:|']
        lines += [f"| `{c['family']}` | `{c['shape']}` | {c['members']} | {c['bytes']} | {c['replaced']} | "
                  f"{c['edge-verified']} | {c['verified']} |" for c in report['families']]
        lines.append('')
    if report['verify']:
        lines += ['| verify.py | Cases | Passed | Failed |', '|---|---:|---:|---:|']
        lines += [f"| `{v['function']}` | {v['cases']} | {v['passed']} | {v['failed']} |" for v in report['verify']]
        lines.append('')
        for v in report['verify']:
            for m in v.get('mutants', []):
                lines.append(f"Mutant `{m['mutant']}` on `{v['family']}`: failed on {m['killed']} of {m['members']} "
                             f"sampled members.")
        if any(v.get('mutants') for v in report['verify']):
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


def structure_errors(d: dict) -> list:
    """The fields render() always writes, with their types: tools that read reports back (and
    reviewers) can rely on them without guessing."""
    errs = []
    if d.get('schema') != 1:
        errs.append('the session report "schema" must be 1 (run tools/session_report.py)')
    for key in ('base', 'branch'):
        if not isinstance(d.get(key), str) or not d[key].strip():
            errs.append(f'the session report "{key}" must be a non-empty string')
    commits = d.get('commits')
    if type(commits) is not int or commits < 0:
        errs.append('the session report "commits" must be a non-negative integer')
    for key in ('captures', 'findings', 'questions'):
        if key in d and not isinstance(d[key], list):
            errs.append(f'the session report "{key}" must be a list')
    for key in ('findings', 'questions'):
        if isinstance(d.get(key), list) and not all(isinstance(x, str) for x in d[key]):
            errs.append(f'the session report "{key}" entries must be strings')
    functions = d.get('functions')
    if isinstance(functions, dict):
        for group, entries in sorted(functions.items()):
            if group not in ('named', 'replaced', 'edge-verified', 'verified'):
                errs.append(f'the session report has an unknown function group "{group}"')
                continue
            for entry in entries if isinstance(entries, list) else []:
                if not isinstance(entry, dict) or not all(
                        isinstance(entry.get(k), str) and entry[k].strip() for k in ('address', 'name')):
                    errs.append(f'each "functions.{group}" entry needs a string address and name')
                    break
    return errs


def family_evidence_errors(d: dict, required_families: list | None) -> list:
    """Every family change in the report needs a clean `verify.py family` summary: all cases of
    all members passing, and every mutant failing on every sampled member."""
    errs = []
    changes = d.get('families', [])
    if not isinstance(changes, list):
        return ['the session report "families" must be a list']
    verify = [v for v in d.get('verify', []) if isinstance(v, dict) and v.get('family')]
    names = set()
    for c in changes:
        if not isinstance(c, dict) or not isinstance(c.get('family'), str):
            errs.append('each "families" entry needs a family name')
            continue
        name = c['family']
        names.add(name)
        matching = [v for v in verify if v.get('family') == name]
        if not matching:
            errs.append(f'no matching verify.py family evidence for family "{name}"')
            continue
        good = any(type(v.get('cases')) is int and v['cases'] > 0 and v.get('passed') == v['cases']
                   and v.get('failed') == 0 and v.get('members') == c.get('members')
                   and v.get('mutants') and all(isinstance(m, dict) and m.get('members') and
                                               m.get('killed') == m.get('members') for m in v['mutants'])
                   for v in matching)
        if not good:
            errs.append(f'verify.py family evidence for "{name}" is incomplete: every member must pass, the member '
                        f'count must match, and every mutant must fail on every sampled member')
    if required_families is not None:
        required = {c['family'] for c in required_families}
        if required - names:
            errs.append('session report omits changed families: ' + ', '.join(sorted(required - names)))
        if names - required:
            errs.append('session report lists families that did not change: ' + ', '.join(sorted(names - required)))
    return errs


def check_body(text: str, required_replacements: list | None = None,
               required_families: list | None = None) -> list:
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
    if not isinstance(d, dict):
        return ['the session report JSON must be an object']
    for key in ('functions', 'verify', 'findings', 'questions'):
        if key not in d:
            errs.append(f'the session report has no "{key}"')
    errs += structure_errors(d)
    functions = d.get('functions')
    if not isinstance(functions, dict):
        errs.append('the session report "functions" must be an object')
        functions = {}
    replacement_groups = ('replaced', 'edge-verified', 'verified')
    replacements = []
    for group in replacement_groups:
        entries = functions.get(group, [])
        if not isinstance(entries, list):
            errs.append(f'the session report "functions.{group}" must be a list')
            continue
        replacements.extend(entries)
    verify = d.get('verify', [])
    if not isinstance(verify, list):
        errs.append('the session report "verify" must be a list')
        verify = []
    for v in verify:
        if not isinstance(v, dict):
            errs.append('verify.py evidence entries must be objects')
            continue
        failed = v.get('failed', 0)
        if isinstance(failed, int) and not isinstance(failed, bool) and failed:
            errs.append(f"verify.py reports {failed} failing case(s) for {v.get('function')}")

    replacement_names = set()
    for replacement in replacements:
        if not isinstance(replacement, dict) or not isinstance(replacement.get('name'), str) \
                or not replacement['name'].strip():
            errs.append('each replaced function needs a non-empty function name')
            continue
        name = replacement['name']
        replacement_names.add(name)
        matching = [v for v in verify if isinstance(v, dict) and v.get('function') == name]
        if not matching:
            errs.append(f'no matching verify.py evidence for replaced function "{name}"')
            continue
        valid = any(
            type(v.get('cases')) is int and v['cases'] > 0
            and type(v.get('passed')) is int and v['passed'] == v['cases']
            and type(v.get('failed')) is int and v['failed'] == 0
            for v in matching
        )
        if not valid:
            errs.append(f'verify.py evidence for replaced function "{name}" is empty or incomplete')

    errs += family_evidence_errors(d, required_families)
    if required_replacements is not None:
        required_names = {item['name'] for item in required_replacements}
        missing = sorted(required_names - replacement_names)
        extra = sorted(replacement_names - required_names)
        if missing:
            errs.append('session report omits changed replacements: ' + ', '.join(missing))
        if extra:
            errs.append('session report lists replacements not changed from original: ' + ', '.join(extra))
    return errs


def read_worktree(path: str) -> str | None:
    full = os.path.join(ROOT, path)
    if not os.path.isfile(full):
        return None
    with open(full) as f:
        return f.read()


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
              'families': branch_family_changes(base, read_worktree),
              'captures': capture_counts(a.captures), 'findings': findings, 'questions': questions}
    sys.stdout.write(render(report))
    return 0


if __name__ == '__main__':
    sys.exit(main())
