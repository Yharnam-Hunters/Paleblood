# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/combined_check.py on a throwaway repository: a pull request that passes alone but breaks
with another, one in text conflict, one that combines cleanly, one already included and one
broken on its own; and the choice of which pull requests to pair with."""
import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest

TOOL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'combined_check.py')
IDENTITY = ['-c', 'user.name=test', '-c', 'user.email=test@localhost']
_spec = importlib.util.spec_from_file_location('combined_check', TOOL)
combined_check = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(combined_check)


class SelectPairs(unittest.TestCase):
    def test_directories_leave_out_the_top_level(self):
        self.assertEqual(combined_check.directories({'CMakeLists.txt', 'game/a/b.cpp', 'runtime/x.c'}),
                         {'game/a', 'runtime'})

    def test_only_related_pull_requests_are_chosen(self):
        own = {'game/render/a.cpp', 'symbols/functions.csv'}
        others = [('#1', {'game/camera/c.cpp'}),             # unrelated
                  ('#2', {'game/render/b.cpp'}),             # same directory
                  ('#3', {'symbols/functions.csv'}),         # same file
                  ('#4', {'README.md'}),                     # top level only: unrelated
                  ('#5', set())]                             # git could not tell: unrelated
        self.assertEqual(combined_check.select_pairs(own, others, None), (['#2', '#3'], ['#1', '#4', '#5']))

    def test_the_closest_are_kept_within_the_limit(self):
        own = {'game/render/a.cpp', 'game/render/b.cpp', 'runtime/x.c'}
        others = [('#1', {'game/render/z.cpp'}),                          # one directory
                  ('#2', {'game/render/a.cpp'}),                          # one file
                  ('#3', {'game/render/a.cpp', 'game/render/b.cpp'}),     # two files
                  ('#4', {'game/render/y.cpp', 'runtime/y.c'}),           # two directories
                  ('#5', {'game/render/w.cpp'})]                          # ties #1, given later
        chosen, skipped = combined_check.select_pairs(own, others, 3)
        self.assertEqual(chosen, ['#2', '#3', '#4'], 'given order is kept in the result')
        self.assertEqual(skipped, ['#1', '#5'])
        self.assertEqual(combined_check.select_pairs(own, others, 4)[0], ['#1', '#2', '#3', '#4'])
        self.assertEqual(combined_check.select_pairs(own, others, 0), ([], ['#1', '#2', '#3', '#4', '#5']))

    def test_overlap_counts(self):
        self.assertEqual(combined_check.overlap({'a/b', 'a/c', 'd/e'}, {'a/b', 'd/f'}), (1, 2))


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
        return self.run_tool_with([], *others)

    def run_tool_with(self, options, *others):
        summary = os.path.join(self.dir, '..', os.path.basename(self.dir) + '.md')
        result = subprocess.run([sys.executable, TOOL, '--check', 'sh check.sh', '--summary', summary, *options, *others],
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

    def test_overlap_and_limit(self):
        self.branch('also-beta', {'names/two': 'beta\n'})          # same directory as the pull request
        self.branch('elsewhere', {'other/file': 'x\n'})            # nothing in common
        self.branch('same-file', {'names/one': 'beta\n'})          # the same file: closest
        self.git('checkout', '-q', '-b', 'pr', 'main')
        self.write('names/one', 'beta\n')
        self.commit('pr')

        code, report = self.run_tool_with(['--base', 'main', '--overlap', '--max-pairs', '1'],
                                          '#2=also-beta', '#3=elsewhere', '#4=same-file')
        self.assertEqual(code, 0)
        self.assertIn('| #4 | already included |', report)
        self.assertNotIn('| #2 |', report)
        self.assertIn('Not checked together (no changed file or directory in common, or beyond the closest 1): #2, #3',
                      report)

        code, report = self.run_tool_with(['--base', 'main', '--overlap'], '#2=also-beta', '#3=elsewhere')
        self.assertEqual(code, 1, 'the related pull request is still checked and breaks together')
        self.assertIn('| #2 | breaks together |', report)
        self.assertIn('Not checked together (no changed file or directory in common): #3', report)

        code, report = self.run_tool_with(['--max-pairs', '1'], '#3=elsewhere', '#2=also-beta')
        self.assertEqual(code, 0)
        self.assertIn('| #3 | builds and passes together |', report)
        self.assertIn('Not checked together (beyond the first 1): #2', report)

    def test_overlap_needs_base(self):
        result = subprocess.run([sys.executable, TOOL, '--overlap', 'x'], cwd=self.dir, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--overlap needs --base', result.stderr)


if __name__ == '__main__':
    unittest.main()
