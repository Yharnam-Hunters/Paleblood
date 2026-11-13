#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bring a branch up to date with main: merge, resolve the shared tables, rebuild, validate.

Run it in the worktree of the branch (clean, no merge in progress). It
  1. fetches origin and merges origin/main (a merge commit, never a rebase: nothing published is
     rewritten),
  2. leaves the generated progress text (GENERATED_MARKDOWN: the blocks of STATUS.md,
     CONTRIBUTING.md and docs/OVERVIEW.md) as main has it: feature branches do not
     regenerate it, tools/progress_pr.py does on main,
  3. resolves conflicts in the keyed tables row by row (TABLES below): a row only one side added,
     changed or removed takes that side; a row both sides changed differently is a real conflict
     and stops the merge for you, except in dated test records (verification, quarantine and
     matching rows), where the newer row wins; rows come out in the order the validators require,
  4. rebuilds (cmake --build build, when build/ exists) and validates
     (tools/validate_functions.py, tools/progress.py --check-touched),
  5. commits the merge; with --push pushes it through the normal pre-push gate, and with --pr N
     re-arms auto-merge for that pull request at the new head.

Any other conflicted file is left for you: resolve it, `git add` it, and run
`tools/remerge.py --continue` to finish steps 2 to 5.

usage: remerge.py [--remote origin] [--continue] [--no-build] [--push] [--pr N]
                  [--repo OWNER/NAME] [--co-author 'Name <email>']
exit status: 0 merged (or already up to date), 1 error, 3 conflicts left for you, 4 build or
validation failed (the merge is staged, not committed)
"""
from __future__ import annotations

import argparse
import csv
import fnmatch
import os
import re
import subprocess
import sys
import tempfile
import time

GENERATED_MARKDOWN = {'STATUS.md': ('progress',), 'CONTRIBUTING.md': ('status',),
                      'docs/OVERVIEW.md': ('target',)}       # tools/progress.py
# Whole generated files kept as main has them. None at present: the progress map and badges are
# built and published by .github/workflows/progress-map.yml, never committed.
GENERATED_FILES: tuple[str, ...] = ()
CASE_SETS = ('recorded', 'edge', 'option-recorded')     # tools/verification.py


def by_address(row: dict) -> tuple:
    return (int(row['address'], 16),)


def by_record(row: dict) -> tuple:
    cases = CASE_SETS.index(row['cases']) if row['cases'] in CASE_SETS else len(CASE_SETS)
    return (row['function'], cases, row['option'])


# path pattern -> (key columns, or None for whole rows of an append-only log; sort key, or None to
# keep main's order; whether a row both sides changed is resolved by its newer 'date')
TABLES = (
    ('symbols/functions.csv', ('address',), by_address, False),
    ('game/hooks.csv', ('address',), by_address, False),
    ('symbols/matching.csv', ('address',), by_address, True),
    ('symbols/verification.csv', ('function', 'cases', 'option'), by_record, True),
    ('symbols/quarantine.csv', ('function', 'cases', 'option', 'case'),
     lambda r: (*by_record(r), r['case']), True),
    ('symbols/reviews.csv', None, lambda r: (int(r['address'], 16), r['date']), False),
    ('symbols/families.csv', ('family',), None, False),
    ('symbols/families/*.verification.csv', ('function', 'cases', 'option'), by_record, True),
    ('symbols/families/*.matching.csv', ('address',), by_address, True),
    ('symbols/families/*.reviews.csv', None, lambda r: (int(r['address'], 16), r['date']), False),
    ('symbols/families/*.csv', ('address',), by_address, False),
)


def repo_flag(repo: str | None) -> list[str]:
    return ['-R', repo] if repo else []


class Conflict(Exception):
    pass


def table_for(path: str):
    for pattern, key, order, dated in TABLES:
        if fnmatch.fnmatchcase(path, pattern):
            return key, order, dated
    return None


def parse(text: str | None, path: str) -> tuple[str | None, list[tuple[str, dict]]]:
    """Header line and (raw line, row) pairs. Rows are kept as written: quoting is not normalized."""
    if text is None:
        return None, []
    lines = text.splitlines()
    if not lines:
        return None, []
    header = next(csv.reader([lines[0]]))
    rows = []
    for line in lines[1:]:
        if not line.strip():
            continue
        fields = next(csv.reader([line]))
        if line.count('"') % 2:
            raise Conflict(f'{path}: a row spans lines; resolve it by hand')
        if len(fields) != len(header):
            raise Conflict(f'{path}: a row has {len(fields)} fields, the header {len(header)}')
        rows.append((line, dict(zip(header, fields))))
    return lines[0], rows


def keyed(rows: list[tuple[str, dict]], key) -> dict:
    out: dict = {}
    for line, row in rows:
        k = tuple(row[c] for c in key) if key else line
        n = 0
        while (k, n) in out:            # whole-row logs may repeat a row; keep every copy
            n += 1
        if key and n:
            raise Conflict(f'duplicate key {k}')
        out[(k, n)] = (line, row)
    return out


def merge_table(path: str, base: str | None, ours: str | None, theirs: str | None) -> str:
    """Three-way merge of one keyed table, row by row. Raises Conflict for a row both sides
    changed differently (unless it is a dated record and the dates differ)."""
    key, order, dated = table_for(path)
    hb, rb = parse(base, path)
    ho, ro = parse(ours, path)
    ht, rt = parse(theirs, path)
    headers = {h for h in (ho, ht) if h is not None}
    if len(headers) > 1:
        if ho == hb:
            return theirs
        if ht == hb:
            raise Conflict(f'{path}: this branch changed the header')
        raise Conflict(f'{path}: both sides changed the header')
    header = headers.pop() if headers else hb
    b, o, t = keyed(rb, key), keyed(ro, key), keyed(rt, key)
    merged: dict = {}
    problems = []
    for k in list(t) + [k for k in o if k not in t] + [k for k in b if k not in t and k not in o]:
        bv, ov, tv = (x.get(k, (None, None))[0] for x in (b, o, t))
        if ov == tv:
            pick = ov
        elif ov == bv:
            pick = tv
        elif tv == bv:
            pick = ov
        elif dated and ov is not None and tv is not None and o[k][1]['date'] != t[k][1]['date']:
            pick = ov if o[k][1]['date'] > t[k][1]['date'] else tv
        else:
            problems.append(f'{path}: {k[0]} changed on both sides')
            continue
        if pick is not None:
            merged[k] = pick
    if problems:
        raise Conflict('\n'.join(problems))
    lines = list(merged.values())
    if order:
        rows = {line: row for line, row in ro + rt + rb}
        lines.sort(key=lambda line: order(rows[line]))
    return '\n'.join([header] + lines) + '\n'


def block_re(name: str) -> re.Pattern:
    return re.compile(rf'(<!-- {name}:start -->\n)(.*?)(<!-- {name}:end -->)', re.S)


def with_blocks_of(text: str | None, source: str | None, blocks) -> str | None:
    """TEXT with each generated block's body replaced by SOURCE's."""
    if text is None or source is None:
        return text
    for name in blocks:
        src = block_re(name).search(source)
        if src and block_re(name).search(text):
            text = block_re(name).sub(lambda m: m.group(1) + src.group(2) + m.group(3), text, count=1)
    return text


class Repo:
    def __init__(self, root: str):
        self.root = root

    def run(self, argv: list[str], check: bool = True, capture: bool = True) -> subprocess.CompletedProcess:
        r = subprocess.run(argv, cwd=self.root, text=True, capture_output=capture)
        if check and r.returncode:
            if capture:
                sys.stderr.write((r.stdout or '')[-4000:] + (r.stderr or '')[-4000:])
            raise RuntimeError(f"{' '.join(argv[:4])} failed ({r.returncode})")
        return r

    def git(self, *args: str, check: bool = True) -> str:
        return self.run(['git', *args], check=check).stdout.strip()

    def show(self, spec: str) -> str | None:
        r = self.run(['git', 'show', spec], check=False)
        return r.stdout if r.returncode == 0 else None

    def write(self, path: str, text: str) -> None:
        full = os.path.join(self.root, path)
        os.makedirs(os.path.dirname(full) or '.', exist_ok=True)
        with open(full, 'w', newline='') as f:
            f.write(text)

    def read(self, path: str) -> str | None:
        full = os.path.join(self.root, path)
        if not os.path.isfile(full):
            return None
        with open(full, newline='') as f:
            return f.read()


def merge_markdown(repo: Repo, path: str) -> bool:
    """A conflicted generated-text Markdown file: merge it with every side's generated blocks set to
    main's, so only prose can still conflict. True when clean."""
    blocks = GENERATED_MARKDOWN[path]
    base, ours, theirs = (repo.show(f':{n}:{path}') for n in (1, 2, 3))
    if theirs is None or ours is None:
        return False
    base = with_blocks_of(base if base is not None else '', theirs, blocks)
    ours = with_blocks_of(ours, theirs, blocks)
    with tempfile.TemporaryDirectory(prefix='pb-remerge-') as tmp:
        names = []
        for label, text in (('ours', ours), ('base', base), ('theirs', theirs)):
            name = os.path.join(tmp, label)
            with open(name, 'w', newline='') as f:
                f.write(text)
            names.append(name)
        r = subprocess.run(['git', 'merge-file', '-p', '-L', 'this branch', '-L', 'merge base', '-L', 'main',
                            *names], capture_output=True, text=True)
    repo.write(path, r.stdout)
    return r.returncode == 0


def restore_generated(repo: Repo, main: str) -> list[str]:
    """Set the generated text to main's. Returns the paths that changed."""
    changed = []
    for path, blocks in GENERATED_MARKDOWN.items():
        cur = repo.read(path)
        new = with_blocks_of(cur, repo.show(f'{main}:{path}'), blocks)
        if new != cur:
            repo.write(path, new)
            changed.append(path)
    for path in GENERATED_FILES:
        want = repo.show(f'{main}:{path}')
        if want is not None and repo.read(path) != want:
            repo.write(path, want)
            changed.append(path)
    return changed


def resolve_conflicts(repo: Repo) -> list[str]:
    """Resolve what this tool can; returns the paths still conflicted."""
    left = []
    for path in repo.git('diff', '--name-only', '--diff-filter=U').split():
        try:
            if path in GENERATED_FILES:
                repo.git('checkout', '--theirs', '--', path)
            elif path in GENERATED_MARKDOWN:
                if not merge_markdown(repo, path):
                    left.append(path)
                    continue
            elif table_for(path):
                base, ours, theirs = (repo.show(f':{n}:{path}') for n in (1, 2, 3))
                repo.write(path, merge_table(path, base, ours, theirs))
            else:
                left.append(path)
                continue
        except Conflict as e:
            print(f'remerge: {e}', file=sys.stderr)
            left.append(path)
            continue
        repo.git('add', '--', path)
        print(f'remerge: resolved {path}')
    return left


def build_and_validate(repo: Repo, build: bool) -> bool:
    if build and os.path.isdir(os.path.join(repo.root, 'build')):
        print('remerge: cmake --build build')
        if repo.run(['cmake', '--build', 'build', f'-j{os.cpu_count() or 4}'], check=False,
                    capture=False).returncode:
            print('remerge: the build fails', file=sys.stderr)
            return False
    elif build:
        print('remerge: no build/ directory; not rebuilding (configure one to have remerge build it)')
    ok = True
    for argv in (['tools/validate_functions.py'], ['tools/progress.py', '--check-touched', 'HEAD', 'MERGE_HEAD']):
        if os.path.isfile(os.path.join(repo.root, argv[0])):
            if repo.run([sys.executable, *argv], check=False, capture=False).returncode:
                print(f'remerge: {argv[0]} fails', file=sys.stderr)
                ok = False
    return ok


def arm(repo: Repo, number: int, project: str) -> bool:
    head = repo.git('rev-parse', 'HEAD')
    for attempt in range(6):    # GitHub takes a moment to see a pushed head
        r = repo.run(['gh', 'pr', 'merge', str(number), *repo_flag(project), '--auto', '--match-head-commit', head],
                     check=False)
        if r.returncode == 0:
            print(f'remerge: #{number} armed for auto-merge at {head[:12]}')
            return True
        time.sleep(10)
    sys.stderr.write(r.stderr)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--remote', default='origin')
    ap.add_argument('--continue', dest='resume', action='store_true',
                    help='finish a merge whose remaining conflicts you resolved and staged')
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--push', action='store_true')
    ap.add_argument('--pr', type=int)
    ap.add_argument('--repo', help='OWNER/NAME for gh (default: the repository of the remote gh picks)')
    ap.add_argument('--co-author')
    a = ap.parse_args()

    top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True)
    if top.returncode:
        print('remerge: run it inside the worktree of your branch', file=sys.stderr)
        return 1
    repo = Repo(top.stdout.strip())
    main_ref = f'refs/remotes/{a.remote}/main'
    try:
        branch = repo.git('symbolic-ref', '--short', '-q', 'HEAD', check=False)
        if not branch:
            print('remerge: HEAD is detached; check out your branch first', file=sys.stderr)
            return 1
        merging = repo.run(['git', 'rev-parse', '-q', '--verify', 'MERGE_HEAD'], check=False).returncode == 0
        if a.resume:
            if not merging:
                print('remerge: no merge in progress to continue', file=sys.stderr)
                return 1
            main = repo.git('rev-parse', 'MERGE_HEAD')
        else:
            if merging:
                print('remerge: a merge is in progress; finish it with --continue or git merge --abort',
                      file=sys.stderr)
                return 1
            if repo.git('status', '--porcelain', '--untracked-files=no'):
                print('remerge: commit or stash your changes first', file=sys.stderr)
                return 1
            repo.git('fetch', '-q', a.remote, f'+refs/heads/main:{main_ref}')
            main = repo.git('rev-parse', main_ref)
            if repo.run(['git', 'merge-base', '--is-ancestor', main, 'HEAD'], check=False).returncode == 0:
                print(f'remerge: {branch} already contains {a.remote}/main ({main[:12]})')
                return 0
            print(f'remerge: merging {a.remote}/main ({main[:12]}) into {branch}')
            repo.run(['git', 'merge', '-q', '--no-ff', '--no-commit', main], check=False)
            if not repo.run(['git', 'rev-parse', '-q', '--verify', 'MERGE_HEAD'], check=False).returncode == 0:
                print('remerge: git merge did not start (see above)', file=sys.stderr)
                return 1
        left = resolve_conflicts(repo)
        unmerged = repo.git('diff', '--name-only', '--diff-filter=U').split()
        if left or unmerged:
            print('remerge: still conflicted, resolve and `git add` these, then run tools/remerge.py --continue:',
                  file=sys.stderr)
            for path in sorted(set(left) | set(unmerged)):
                print(f'  {path}', file=sys.stderr)
            return 3
        restored = restore_generated(repo, main)
        if restored:
            repo.git('add', '--', *restored)
            print(f"remerge: generated progress text left as main has it: {', '.join(restored)}")
        if not build_and_validate(repo, not a.no_build):
            print('remerge: fix the above, `git add`, then run tools/remerge.py --continue', file=sys.stderr)
            return 4
        message = f'Merge {a.remote}/main into {branch}\n'
        if a.co_author:
            message += f'\nCo-Authored-By: {a.co_author}\n'
        with tempfile.NamedTemporaryFile('w', prefix='pb-remerge-', suffix='.txt', delete=False) as f:
            f.write(message)
        try:
            commit = repo.run(['git', 'commit', '-q', '-F', f.name], check=False, capture=False)
        finally:
            os.remove(f.name)
        if commit.returncode:
            print('remerge: the commit was refused (the pre-commit hook says why above)', file=sys.stderr)
            return 4
        print(f"remerge: merged at {repo.git('rev-parse', '--short', 'HEAD')}")
        if a.push:
            if repo.run(['git', 'push', a.remote, branch], check=False, capture=False).returncode:
                print('remerge: git push failed; the pre-push gate output says why', file=sys.stderr)
                return 1
        if a.pr is not None:
            if not a.push:
                print('remerge: --pr without --push: arming at a head GitHub may not have yet', file=sys.stderr)
            if not arm(repo, a.pr, a.repo):
                return 1
        return 0
    except RuntimeError as e:
        print(f'remerge: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
