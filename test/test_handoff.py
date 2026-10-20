# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/handoff.py, run as a contributor would in a scratch repository: write, show, check."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import handoff  # noqa: E402

HEADER = 'address,name,system,status\n'


def run(cwd, *args, check=True):
    return subprocess.run(args, cwd=cwd, check=check, capture_output=True, text=True)


class HandoffRepo(unittest.TestCase):
    """A two-commit repository with its own copy of the tool, cloned from a GitHub-style URL."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.repo = os.path.join(self.tmp, 'work')
        os.makedirs(os.path.join(self.repo, 'tools'))
        os.makedirs(os.path.join(self.repo, 'symbols'))
        for name in ('handoff.py', 'session_report.py'):
            shutil.copy(os.path.join(ROOT, 'tools', name), os.path.join(self.repo, 'tools', name))
        self.csv = os.path.join(self.repo, 'symbols', 'functions.csv')
        with open(self.csv, 'w') as f:
            f.write(HEADER + '0x1,a,s,original\n')
        with open(os.path.join(self.repo, '.gitignore'), 'w') as f:
            f.write('.handoff/\n__pycache__/\n')
        run(self.repo, 'git', 'init', '-q', '-b', 'main')
        run(self.repo, 'git', 'config', 'user.email', 'a@example.com')
        run(self.repo, 'git', 'config', 'user.name', 'A')
        run(self.repo, 'git', 'remote', 'add', 'origin', 'git@github.com:Owner/Repo.git')
        run(self.repo, 'git', 'add', '-A')
        run(self.repo, 'git', 'commit', '-q', '-m', 'base')
        run(self.repo, 'git', 'update-ref', 'refs/remotes/origin/main', 'HEAD')
        run(self.repo, 'git', 'checkout', '-q', '-b', 'runtime/topic')
        with open(self.csv, 'w') as f:
            f.write(HEADER + '0x1,a,s,replaced\n0x2,b,s,original\n')
        run(self.repo, 'git', 'commit', '-qam', 'work')

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def tool(self, *args, check=True):
        return run(self.repo, sys.executable, 'tools/handoff.py', *args, check=check)

    def saved(self):
        with open(os.path.join(self.repo, '.handoff', 'handoff.json')) as f:
            return json.load(f)

    def test_write_records_the_branch_state(self):
        self.tool('write', '--tool', 'manual', '--task', 'implement a call',
                  '--ownership', 'claimed', '--claim', 'https://github.com/Owner/Repo/issues/34#issuecomment-1',
                  '--test', 'ctest: 13/13 passed', '--next', 'open the pull request')
        d = self.saved()
        self.assertEqual(handoff.check(d), [])
        self.assertEqual(d['repository'], 'Owner/Repo')
        self.assertEqual(d['branch'], 'runtime/topic')
        self.assertEqual(d['worktree'], 'work')
        self.assertEqual(len(d['commit']), 40)
        self.assertEqual([f['address'] for f in d['evidence']['functions']['replaced']], ['0x1'])
        self.assertEqual([f['address'] for f in d['evidence']['functions']['named']], ['0x2'])
        self.assertEqual(d['evidence']['recordings'], 'none')
        self.assertEqual(d['dirty'], {'dirty': False, 'paths': 0})
        self.assertIn('open the pull request', self.tool('show', '--markdown').stdout)
        self.assertEqual(self.tool('check').stdout.strip(), 'handoff: ok')

    def test_next_session_appends_history_and_keeps_the_task(self):
        self.tool('write', '--tool', 'codex', '--task', 'implement a call', '--next', 'write tests')
        first = self.saved()
        with open(os.path.join(self.repo, 'notes.txt'), 'w') as f:
            f.write('wip\n')
        self.tool('write', '--tool', 'claude', '--recordings', 'synthetic', '--next', 'review')
        d = self.saved()
        self.assertEqual(d['task'], 'implement a call')
        self.assertEqual(d['tool'], 'claude')
        self.assertEqual(d['evidence']['recordings'], 'synthetic')
        self.assertEqual(d['dirty'], {'dirty': True, 'paths': 1})
        self.assertEqual(d['history'], [{'tool': 'codex', 'commit': first['commit'],
                                         'written_at': first['written_at'], 'next_action': 'write tests'}])

    def test_first_handoff_needs_a_task(self):
        out = self.tool('write', '--tool', 'manual', '--next', 'x', check=False)
        self.assertNotEqual(out.returncode, 0)
        self.assertIn('--task', out.stderr)

    def test_write_refuses_paths_secrets_and_session_links(self):
        for bad in ('see /home/someone/work/log.txt', 'token ghp_' + 'a' * 36,
                    'https://claude.ai/code/session_123', r'C:\Users\x\log.txt', 'log in ~/scratch'):
            with self.subTest(bad=bad):
                out = self.tool('write', '--tool', 'manual', '--task', 't', '--next', bad, check=False)
                self.assertNotEqual(out.returncode, 0)
                self.assertFalse(os.path.exists(os.path.join(self.repo, '.handoff', 'handoff.json')))


class HandoffCheck(unittest.TestCase):
    def valid(self):
        return {'schema': 1, 'repository': 'Owner/Repo', 'commit': 'a' * 40, 'branch': 'b', 'worktree': 'w',
                'task': 't', 'tool': 'manual', 'ownership': {'state': 'unknown', 'claim': ''},
                'evidence': {'functions': {}, 'verify': [], 'recordings': 'none'}, 'tests': [],
                'blockers': [], 'next_action': 'n', 'dirty': {'dirty': False, 'paths': 0},
                'written_at': '2026-10-20T00:00:00Z', 'history': []}

    def test_valid(self):
        self.assertEqual(handoff.check(self.valid()), [])

    def test_schema_errors(self):
        cases = {
            '"schema"': {'schema': 2},
            '"tool"': {'tool': 'gpt'},
            '"commit"': {'commit': 'abc'},
            '"worktree"': {'worktree': 'a/b'},
            'ownership.state': {'ownership': {'state': 'mine'}},
            'ownership.claim': {'ownership': {'state': 'claimed', 'claim': 'http://example.com'}},
            'evidence.recordings': {'evidence': {'functions': {}, 'verify': [], 'recordings': 'real'}},
            'COMMAND: RESULT': {'tests': ['ran it']},
            'dirty.paths': {'dirty': {'dirty': True, 'paths': -1}},
            '"history"': {'history': None},
        }
        for needle, change in cases.items():
            with self.subTest(needle=needle):
                d = self.valid()
                d.update(change)
                self.assertTrue(any(needle in e for e in handoff.check(d)), handoff.check(d))
        self.assertTrue(handoff.check([]))

    def test_nested_secrets_are_found(self):
        d = self.valid()
        d['blockers'] = ['key AKIA' + 'A' * 16]
        self.assertTrue(any('secret' in e for e in handoff.check(d)))
        d = self.valid()
        d['tests'] = ['cat /mnt/data/x: ok']
        self.assertTrue(any('local path' in e for e in handoff.check(d)))


if __name__ == '__main__':
    unittest.main()
