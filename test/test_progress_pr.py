# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/progress_pr.py: the regenerate branch is rebuilt from main in a throwaway repository."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import progress_pr  # noqa: E402

# Stands in for tools/progress.py: the generated text is the count in count.txt.
FAKE_PROGRESS = '''import sys
count = open('count.txt').read().strip()
if '--update-readme' in sys.argv:
    open('STATUS.md', 'w').write('progress ' + count + '\\n')
print('status ' + count)
'''


def git(cwd, *args):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


class RegenerateBranch(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        hooks = os.path.join(self.tmp.name, 'no-hooks')
        os.mkdir(hooks)
        self.origin = os.path.join(self.tmp.name, 'origin')
        os.mkdir(self.origin)
        git(self.origin, 'init', '-q', '-b', 'main')
        git(self.origin, 'config', 'core.hooksPath', hooks)
        self.write(self.origin, 'tools/progress.py', FAKE_PROGRESS)
        self.write(self.origin, 'count.txt', '1\n')
        self.write(self.origin, 'STATUS.md', 'progress 1\n')
        self.write(self.origin, 'README.md', 'readme\n')
        self.write(self.origin, 'docs/assets/status.svg', '<svg/>\n')
        self.commit(self.origin, 'base')
        self.clone = os.path.join(self.tmp.name, 'clone')
        git(self.tmp.name, 'clone', '-q', self.origin, self.clone)
        git(self.clone, 'config', 'core.hooksPath', hooks)
        self.wt = os.path.join(self.tmp.name, 'worktree')
        git(self.clone, 'worktree', 'add', '-q', '--detach', self.wt, 'HEAD')
        for repo in (self.origin, self.clone):
            git(repo, 'config', 'user.name', 'test')
            git(repo, 'config', 'user.email', 'test@example.invalid')

    def tearDown(self):
        self.tmp.cleanup()

    def merging(self):
        return subprocess.run(['git', 'rev-parse', '-q', '--verify', 'MERGE_HEAD'],
                              cwd=self.wt, capture_output=True).returncode == 0

    @staticmethod
    def write(repo, path, text):
        full = os.path.join(repo, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'w') as f:
            f.write(text)

    @staticmethod
    def commit(repo, message):
        git(repo, 'add', '-A')
        git(repo, '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
            'commit', '-q', '-m', message)

    def open_branch_that_main_left_behind(self):
        """An open regenerate branch that also changed README.md and the status image, as before
        they stopped being generated; main then deleted the image and changed README.md."""
        git(self.origin, 'checkout', '-q', '-b', progress_pr.BRANCH)
        self.write(self.origin, 'STATUS.md', 'progress 2\n')
        self.write(self.origin, 'README.md', 'readme with progress 2\n')
        self.write(self.origin, 'docs/assets/status.svg', '<svg>2</svg>\n')
        self.commit(self.origin, 'regenerate')
        git(self.origin, 'checkout', '-q', 'main')
        os.remove(os.path.join(self.origin, 'docs/assets/status.svg'))
        self.write(self.origin, 'README.md', 'short readme\n')
        self.write(self.origin, 'count.txt', '3\n')
        self.commit(self.origin, 'main moves on')
        return git(self.origin, 'rev-parse', progress_pr.BRANCH), git(self.origin, 'rev-parse', 'main')

    def test_conflicting_open_branch_is_rebuilt_from_main(self):
        branch, main = self.open_branch_that_main_left_behind()
        main_sha, changed, pending = progress_pr.prepare(self.wt, 'origin')
        self.assertEqual((main_sha, changed, pending), (main, ['STATUS.md'], True))
        git(self.wt, '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
            'commit', '-q', '-m', 'regenerate')
        head = git(self.wt, 'rev-parse', 'HEAD')
        # the branch moves forward (a plain push), and main's tree is taken whole
        self.assertTrue(progress_pr.is_ancestor(branch, head, cwd=self.wt))
        self.assertTrue(progress_pr.is_ancestor(main, head, cwd=self.wt))
        self.assertEqual(git(self.wt, 'diff', '--name-only', main, head), 'STATUS.md')
        self.assertEqual(git(self.wt, 'show', 'HEAD:STATUS.md'), 'progress 3')
        self.assertEqual(git(self.wt, 'show', 'HEAD:README.md'), 'short readme')
        self.assertEqual(git(self.wt, 'status', '--porcelain', '--untracked-files=no'), '')

    def test_merged_branch_continues_from_main(self):
        git(self.origin, 'branch', progress_pr.BRANCH)
        self.write(self.origin, 'count.txt', '2\n')
        self.commit(self.origin, 'main moves on')
        main_sha, changed, pending = progress_pr.prepare(self.wt, 'origin')
        self.assertTrue(pending)
        self.assertFalse(self.merging())
        git(self.wt, '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
            'commit', '-q', '-m', 'regenerate')
        self.assertEqual(git(self.wt, 'rev-parse', 'HEAD^'), main_sha)
        self.assertEqual(len(git(self.wt, 'rev-list', '--parents', '-n1', 'HEAD').split()), 2)

    def test_current_main_leaves_nothing_to_push(self):
        branch, main = self.open_branch_that_main_left_behind()
        self.write(self.origin, 'STATUS.md', 'progress 3\n')
        self.commit(self.origin, 'main regenerated')
        main_sha, changed, pending = progress_pr.prepare(self.wt, 'origin')
        self.assertEqual((changed, pending), ([], False))
        self.assertFalse(self.merging())
        self.assertEqual(git(self.wt, 'rev-parse', 'HEAD'), main_sha)


if __name__ == '__main__':
    unittest.main()
