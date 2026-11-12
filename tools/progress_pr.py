#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate main's progress text in one pull request, and queue it.

Feature branches leave the generated progress text alone (the progress and status blocks of
README.md, STATUS.md and CONTRIBUTING.md, and the status card docs/assets/status-*.svg), so
parallel pull requests do not conflict on it. This tool brings main up to date instead: in a
temporary worktree of origin/main it runs `tools/progress.py --update-readme`, commits the result
to the branch auto/regenerate-progress, pushes it through the normal pre-push gate, opens a
documentation-only pull request for it (or reuses the open one) and enables auto-merge, which
puts it in the merge queue. Run it after any of your pull requests merges; .github/workflows/
progress.yml fails while main is behind.

It is idempotent and safe to run from several sessions: the branch only ever moves forward (an
open pull request's branch gets origin/main merged into it, a merged one is continued from
origin/main), a push that loses a race is retried from the new state, and nothing is pushed when
main is already current. It never force-pushes and never touches your own worktree.

usage: progress_pr.py [--remote origin] [--repo OWNER/NAME] [--co-author 'Name <email>']
                      [--dry-run] [--no-queue]
  --co-author   adds a Co-Authored-By trailer to the commit (AI-assisted sessions)
  --dry-run     regenerate and report what would change; push nothing
  --no-queue    push and open the pull request, but do not enable auto-merge
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRANCH = 'auto/regenerate-progress'
DEFAULT_REPO = 'Yharnam-Hunters/Paleblood'
GENERATED = ('README.md', 'STATUS.md', 'CONTRIBUTING.md',
             'docs/assets/status-light.svg', 'docs/assets/status-dark.svg')
ATTEMPTS = 3


class Failure(Exception):
    pass


def run(argv: list[str], cwd: str = ROOT, check: bool = True, quiet: bool = False) -> subprocess.CompletedProcess:
    r = subprocess.run(argv, cwd=cwd, text=True, capture_output=True)
    if check and r.returncode:
        if not quiet:
            sys.stderr.write(r.stdout[-4000:] + r.stderr[-4000:])
        raise Failure(f"{' '.join(argv[:3])} failed ({r.returncode})")
    return r


def git(*args: str, cwd: str = ROOT, check: bool = True) -> str:
    return run(['git', *args], cwd=cwd, check=check).stdout.strip()


def is_ancestor(a: str, b: str) -> bool:
    return run(['git', 'merge-base', '--is-ancestor', a, b], check=False).returncode == 0


def open_pr(repo: str) -> int | None:
    out = run(['gh', 'pr', 'list', '-R', repo, '--head', BRANCH, '--state', 'open',
               '--json', 'number', '--jq', '.[].number']).stdout.split()
    return int(out[0]) if out else None


def report_body(main_sha: str, paths: list[str]) -> str:
    summary = (f'Regenerated the progress blocks and the status card from symbols/ on main {main_sha[:12]} '
               '(tools/progress_pr.py). No other text changed.')
    report = {'schema': 1, 'track': 'documentation', 'pages': paths, 'summary': summary,
              'implementation_changes': False}
    return ('### Documentation-only report\n\n'
            f"Pages changed: {', '.join(f'`{p}`' for p in paths)}\n\n"
            f'Summary: {summary}\n\n'
            'Main is regenerated here instead of in every feature branch, so parallel pull requests '
            'do not conflict on these lines (CONTRIBUTING.md, "Generated progress and merging main").\n\n'
            '<!-- documentation-report:start -->\n'
            f'<!-- documentation-report-json {json.dumps(report)} -->\n'
            '<!-- documentation-report:end -->\n')


def prepare(wt: str, remote: str) -> tuple[str, list[str], bool]:
    """Build the next commit of the branch in the worktree WT. Returns (main sha, changed generated
    paths relative to main, whether there is anything to push)."""
    main_ref = f'refs/remotes/{remote}/main'
    branch_ref = f'refs/remotes/{remote}/{BRANCH}'
    git('fetch', '-q', remote, f'+refs/heads/main:{main_ref}')
    have_branch = run(['git', 'fetch', '-q', remote, f'+refs/heads/{BRANCH}:{branch_ref}'],
                      check=False, quiet=True).returncode == 0
    main_sha = git('rev-parse', main_ref)
    continue_branch = have_branch and not is_ancestor(branch_ref, main_ref)
    git('checkout', '-q', '--detach', branch_ref if continue_branch else main_sha, cwd=wt)
    if continue_branch:
        # the open pull request's branch: bring main in; its only changes are generated text,
        # which is regenerated below, so main's side of any conflict is taken
        merged = run(['git', 'merge', '-q', '--no-commit', '--no-ff', main_sha], cwd=wt, check=False)
        if merged.returncode:
            conflicted = git('diff', '--name-only', '--diff-filter=U', cwd=wt).split()
            other = [p for p in conflicted if p not in GENERATED]
            if other:
                git('merge', '--abort', cwd=wt, check=False)
                raise Failure(f'{BRANCH} conflicts with main outside the generated files: {", ".join(other)}')
            git('checkout', '--theirs', '--', *conflicted, cwd=wt)
    run([sys.executable, 'tools/progress.py', '--update-readme'], cwd=wt)
    git('add', '--', *[p for p in GENERATED if os.path.exists(os.path.join(wt, p))], cwd=wt)
    changed = git('diff', '--cached', '--name-only', main_sha, '--', *GENERATED, cwd=wt).split()
    pending = bool(git('diff', '--cached', '--name-only', cwd=wt)) or continue_branch
    return main_sha, changed, pending and bool(changed)


def commit_message(wt: str, main_sha: str, co_author: str | None) -> str:
    status = run([sys.executable, 'tools/progress.py', '--status-line'], cwd=wt).stdout.strip()
    msg = f'docs: regenerate progress from symbols/ on main {main_sha[:12]}\n\n{status}\n'
    if co_author:
        msg += f'\nCo-Authored-By: {co_author}\n'
    return msg


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--remote', default='origin')
    ap.add_argument('--repo', default=DEFAULT_REPO)
    ap.add_argument('--co-author')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--no-queue', action='store_true')
    a = ap.parse_args()

    parent = tempfile.mkdtemp(prefix='pb-progress-')
    wt = os.path.join(parent, 'worktree')
    git('worktree', 'add', '-q', '--detach', wt, 'HEAD')
    try:
        for attempt in range(1, ATTEMPTS + 1):
            main_sha, changed, pending = prepare(wt, a.remote)
            if not pending:
                number = open_pr(a.repo) if not a.dry_run else None
                if number:
                    print(f'progress-pr: main {main_sha[:12]} is current; pull request #{number} is no longer '
                          'needed', file=sys.stderr)
                    run(['gh', 'pr', 'close', str(number), '-R', a.repo, '--comment',
                         f'Main {main_sha[:12]} already has current progress text (tools/progress_pr.py).'])
                else:
                    print(f'progress-pr: main {main_sha[:12]} is current; nothing to do')
                git('merge', '--abort', cwd=wt, check=False)
                return 0
            if a.dry_run:
                print(f"progress-pr: would regenerate {', '.join(changed)} from main {main_sha[:12]}")
                print(git('diff', '--cached', '--stat', main_sha, cwd=wt))
                git('merge', '--abort', cwd=wt, check=False)
                return 0
            msg_path = os.path.join(parent, 'message')
            with open(msg_path, 'w') as f:
                f.write(commit_message(wt, main_sha, a.co_author))
            git('commit', '-q', '-F', msg_path, cwd=wt)
            head = git('rev-parse', 'HEAD', cwd=wt)
            pushed = run(['git', 'push', a.remote, f'HEAD:refs/heads/{BRANCH}'], cwd=wt, check=False)
            if pushed.returncode == 0:
                break
            sys.stderr.write(pushed.stdout[-3000:] + pushed.stderr[-3000:])
            if 'non-fast-forward' not in pushed.stderr and 'fetch first' not in pushed.stderr:
                raise Failure('git push failed (see above); the pre-push gate output says why')
            print(f'progress-pr: lost a race for {BRANCH} (attempt {attempt}); retrying', file=sys.stderr)
        else:
            raise Failure(f'could not push {BRANCH} after {ATTEMPTS} attempts')

        number = open_pr(a.repo)
        if number is None:
            body_path = os.path.join(parent, 'body.md')
            with open(body_path, 'w') as f:
                f.write(report_body(main_sha, changed))
            url = run(['gh', 'pr', 'create', '-R', a.repo, '--base', 'main', '--head', BRANCH,
                       '--title', 'docs: regenerate progress', '--body-file', body_path]).stdout.strip()
            number = int(url.rstrip('/').rsplit('/', 1)[-1])
        print(f'progress-pr: pushed {head[:12]} to {BRANCH}; pull request #{number}')
        if not a.no_queue:
            run(['gh', 'pr', 'merge', str(number), '-R', a.repo, '--auto', '--match-head-commit', head])
            print(f'progress-pr: #{number} queued (auto-merge)')
        return 0
    except Failure as e:
        print(f'progress-pr: {e}', file=sys.stderr)
        return 1
    finally:
        git('worktree', 'remove', '--force', wt, check=False)
        for leftover in ('message', 'body.md'):
            path = os.path.join(parent, leftover)
            if os.path.exists(path):
                os.remove(path)
        if os.path.isdir(parent) and not os.listdir(parent):
            os.rmdir(parent)


if __name__ == '__main__':
    sys.exit(main())
