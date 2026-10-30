# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/combined_check.py on a throwaway repository: a pull request that passes alone but breaks
with another, one in text conflict, one that combines cleanly, one already included and one
broken on its own."""
import os
import subprocess
import sys
import tempfile
import unittest

TOOL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'combined_check.py')
IDENTITY = ['-c', 'user.name=test', '-c', 'user.email=test@localhost']


class CombinedCheck(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.git('init', '-q', '-b', 'main')
        # The check: no name may be defined twice across names/*.
        self.write('check.sh', 'test -z "$(cat names/* | sort | uniq -d)"\n')
        self.write('names/base', 'alpha\n')
        self.write('shared', 'one\n')
        self.commit('base')

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        return subprocess.run(['git', *IDENTITY, *args], cwd=self.dir, check=True, capture_output=True, text=True).stdout

    def write(self, path, text):
        os.makedirs(os.path.dirname(os.path.join(self.dir, path)) or self.dir, exist_ok=True)
        with open(os.path.join(self.dir, path), 'w') as f:
            f.write(text)

    def commit(self, message):
        self.git('add', '-A')
        self.git('commit', '-q', '-m', message)

    def branch(self, name, files):
        self.git('checkout', '-q', '-b', name, 'main')
        for path, text in files.items():
            self.write(path, text)
        self.commit(name)

    def run_tool(self, *others):
        summary = os.path.join(self.dir, '..', os.path.basename(self.dir) + '.md')
        result = subprocess.run([sys.executable, TOOL, '--check', 'sh check.sh', '--summary', summary, *others],
                                cwd=self.dir, capture_output=True, text=True)
        with open(summary) as f:
            report = f.read()
        os.remove(summary)
        return result.returncode, report

    def test_pairs(self):
        self.branch('defines-beta', {'names/one': 'beta\n'})
        self.branch('also-beta', {'names/two': 'beta\n'})
        self.branch('edits-shared', {'shared': 'two\n'})
        self.branch('edits-shared-too', {'shared': 'three\n'})
        self.branch('gamma', {'names/three': 'gamma\n'})
        self.branch('broken-alone', {'names/four': 'alpha\n'})
        self.git('checkout', '-q', '-b', 'pr', 'main')
        self.git('merge', '-q', '--no-edit', 'defines-beta')
        self.git('merge', '-q', '--no-edit', 'edits-shared')
        head = self.git('rev-parse', 'HEAD')

        code, report = self.run_tool('--base', 'main', '#2=also-beta', '#3=edits-shared-too', '#4=gamma', '#5=defines-beta',
                                     '#6=broken-alone')
        self.assertEqual(code, 1)
        self.assertIn('| #2 | breaks together |', report)
        self.assertIn('`sh check.sh` fails', report)
        self.assertIn('| #3 | text conflict in shared;', report)
        self.assertIn('| #4 | builds and passes together |', report)
        self.assertIn('| #5 | already included |', report)
        self.assertIn('| #6 | fails on its own on the base too; not checked together |', report)
        self.assertEqual((self.git('rev-parse', 'HEAD'), self.git('branch', '--show-current')), (head, 'pr\n'),
                         'the checkout is restored')
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_all_combine(self):
        self.branch('gamma', {'names/three': 'gamma\n'})
        self.git('checkout', '-q', 'main')
        self.assertEqual(self.run_tool('#4=gamma'), (0, '### Combined with the other open pull requests\n\n'
                                                        '| Pull request | Result |\n|---|---|\n'
                                                        '| #4 | builds and passes together |\n'))
        self.assertEqual(self.run_tool()[1], '### Combined with the other open pull requests\n\nNo other open pull requests.\n')


if __name__ == '__main__':
    unittest.main()
