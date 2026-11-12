# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/remerge.py: keyed three-way table merges and the whole merge in a throwaway repository."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import remerge  # noqa: E402

FUNCS = 'address,size,name,system,status,notes\n'
VERIF = 'function,cases,option,total,passed,failed,quarantined,source,date\n'
REVIEWS = 'address,name,date,reviewer,verdict,notes\n'


class MergeTable(unittest.TestCase):
    def test_rows_added_next_to_each_other_on_both_sides(self):
        base = FUNCS + '0x00401000,8,a,engine,original,\n0x00403000,8,c,engine,original,\n'
        ours = FUNCS + '0x00401000,8,a,engine,original,\n0x00402800,8,b2,engine,replaced,\n0x00403000,8,c,engine,original,\n'
        theirs = FUNCS + '0x00401000,8,a,engine,original,\n0x00402000,8,b1,loading,replaced,\n0x00403000,8,c,engine,original,\n'
        out = remerge.merge_table('symbols/functions.csv', base, ours, theirs)
        self.assertEqual(out, FUNCS + '0x00401000,8,a,engine,original,\n0x00402000,8,b1,loading,replaced,\n'
                              '0x00402800,8,b2,engine,replaced,\n0x00403000,8,c,engine,original,\n')

    def test_a_row_changed_on_one_side_next_to_a_row_added_on_the_other(self):
        base = FUNCS + '0x00401000,8,a,engine,edge-verified,\n0x00403000,8,c,engine,original,\n'
        ours = FUNCS + '0x00401000,8,a,engine,verified,\n0x00403000,8,c,engine,original,\n'
        theirs = FUNCS + '0x00401000,8,a,engine,edge-verified,\n0x00402000,8,b,engine,replaced,"x, y"\n0x00403000,8,c,engine,original,\n'
        out = remerge.merge_table('symbols/functions.csv', base, ours, theirs)
        self.assertEqual(out, FUNCS + '0x00401000,8,a,engine,verified,\n0x00402000,8,b,engine,replaced,"x, y"\n'
                              '0x00403000,8,c,engine,original,\n')

    def test_a_row_both_sides_changed_differently_stops(self):
        base = FUNCS + '0x00401000,8,a,engine,edge-verified,\n'
        ours = FUNCS + '0x00401000,8,a,engine,verified,\n'
        theirs = FUNCS + '0x00401000,8,a,engine,edge-verified,new note\n'
        with self.assertRaises(remerge.Conflict):
            remerge.merge_table('symbols/functions.csv', base, ours, theirs)

    def test_a_row_removed_on_one_side_stays_removed(self):
        base = FUNCS + '0x00401000,8,a,engine,original,\n0x00402000,8,b,engine,original,\n'
        ours = FUNCS + '0x00401000,8,a,engine,original,\n'
        theirs = base + '0x00403000,8,c,engine,original,\n'
        out = remerge.merge_table('symbols/functions.csv', base, ours, theirs)
        self.assertEqual(out, FUNCS + '0x00401000,8,a,engine,original,\n0x00403000,8,c,engine,original,\n')

    def test_the_newer_test_record_wins(self):
        base = VERIF + 'f,edge,,3,3,0,0,aaaa,2026-11-01\n'
        ours = VERIF + 'f,recorded,,9,9,0,0,bbbb,2026-11-12\nf,edge,,3,3,0,0,bbbb,2026-11-12\n'
        theirs = VERIF + 'f,edge,,4,4,0,0,cccc,2026-11-10\ng,edge,,1,1,0,0,dddd,2026-11-10\n'
        out = remerge.merge_table('symbols/verification.csv', base, ours, theirs)
        self.assertEqual(out, VERIF + 'f,recorded,,9,9,0,0,bbbb,2026-11-12\nf,edge,,3,3,0,0,bbbb,2026-11-12\n'
                              'g,edge,,1,1,0,0,dddd,2026-11-10\n')

    def test_reviews_are_a_log_in_address_and_date_order(self):
        base = REVIEWS + '0x00402000,b,2026-11-01,s,approved,\n'
        ours = base + '0x00401000,a,2026-11-12,s,approved,\n0x00402000,b,2026-11-12,s,approved,again\n'
        theirs = REVIEWS + '0x00401000,a,2026-11-03,s,changes requested,\n0x00402000,b,2026-11-01,s,approved,\n'
        out = remerge.merge_table('symbols/reviews.csv', base, ours, theirs)
        self.assertEqual(out, REVIEWS + '0x00401000,a,2026-11-03,s,changes requested,\n'
                              '0x00401000,a,2026-11-12,s,approved,\n0x00402000,b,2026-11-01,s,approved,\n'
                              '0x00402000,b,2026-11-12,s,approved,again\n')

    def test_family_tables_are_keyed_by_their_kind(self):
        self.assertEqual(remerge.table_for('symbols/families/returns_zero.csv')[0], ('address',))
        self.assertEqual(remerge.table_for('symbols/families/returns_zero.verification.csv')[0],
                         ('function', 'cases', 'option'))
        self.assertIsNone(remerge.table_for('symbols/families/returns_zero.reviews.csv')[0])
        self.assertIsNone(remerge.table_for('symbols/ghidra_functions.csv'))


class GeneratedText(unittest.TestCase):
    def test_blocks_take_the_source_text_and_prose_stays(self):
        text = 'intro\n<!-- progress:start -->\nold\n<!-- progress:end -->\nmine\n'
        main = 'other intro\n<!-- progress:start -->\nnew\n<!-- progress:end -->\ntheirs\n'
        self.assertEqual(remerge.with_blocks_of(text, main, ('progress', 'status')),
                         'intro\n<!-- progress:start -->\nnew\n<!-- progress:end -->\nmine\n')


def git(cwd, *args):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


class WholeMerge(unittest.TestCase):
    """A branch and main that both add a function row next to the same row and both regenerate the
    README progress block: the merge keeps both rows, in order, and main's generated text."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='pb-remerge-test-')
        self.addCleanup(self.tmp.cleanup)
        env = {'GIT_AUTHOR_NAME': 'T', 'GIT_AUTHOR_EMAIL': 't@example.invalid',
               'GIT_COMMITTER_NAME': 'T', 'GIT_COMMITTER_EMAIL': 't@example.invalid'}
        self.old_env = {k: os.environ.get(k) for k in env}
        os.environ.update(env)
        self.addCleanup(self.restore_env)
        self.origin = os.path.join(self.tmp.name, 'origin')
        os.makedirs(os.path.join(self.origin, 'symbols'))
        git(self.origin, 'init', '-q', '-b', 'main')
        git(self.origin, 'config', 'core.hooksPath', os.path.join(self.tmp.name, 'no-hooks'))
        self.write(self.origin, 'symbols/functions.csv', FUNCS + '0x00401000,8,a,engine,original,\n'
                   '0x00403000,8,c,engine,original,\n')
        self.write(self.origin, 'README.md', 'Intro.\n<!-- progress:start -->\n1 replaced\n<!-- progress:end -->\n')
        git(self.origin, 'add', '-A')
        git(self.origin, 'commit', '-q', '-m', 'base')
        self.clone = os.path.join(self.tmp.name, 'clone')
        git(self.tmp.name, 'clone', '-q', self.origin, self.clone)
        git(self.clone, 'config', 'core.hooksPath', os.path.join(self.tmp.name, 'no-hooks'))
        git(self.clone, 'checkout', '-q', '-b', 'feature')

    def restore_env(self):
        for k, v in self.old_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    @staticmethod
    def write(repo, path, text):
        with open(os.path.join(repo, path), 'w') as f:
            f.write(text)

    def test_merge(self):
        self.write(self.origin, 'symbols/functions.csv', FUNCS + '0x00401000,8,a,engine,original,\n'
                   '0x00402000,8,b1,loading,replaced,\n0x00403000,8,c,engine,original,\n')
        self.write(self.origin, 'README.md', 'Intro.\n<!-- progress:start -->\n2 replaced\n<!-- progress:end -->\n')
        git(self.origin, 'commit', '-q', '-am', 'main adds b1')
        self.write(self.clone, 'symbols/functions.csv', FUNCS + '0x00401000,8,a,engine,original,\n'
                   '0x00402800,8,b2,engine,replaced,\n0x00403000,8,c,engine,original,\n')
        self.write(self.clone, 'README.md', 'Intro, edited.\n<!-- progress:start -->\n9 replaced\n<!-- progress:end -->\n')
        git(self.clone, 'commit', '-q', '-am', 'feature adds b2')

        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'remerge.py'), '--no-build'],
                           cwd=self.clone, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(os.path.join(self.clone, 'symbols/functions.csv')) as f:
            self.assertEqual(f.read(), FUNCS + '0x00401000,8,a,engine,original,\n0x00402000,8,b1,loading,replaced,\n'
                                       '0x00402800,8,b2,engine,replaced,\n0x00403000,8,c,engine,original,\n')
        with open(os.path.join(self.clone, 'README.md')) as f:
            self.assertEqual(f.read(), 'Intro, edited.\n<!-- progress:start -->\n2 replaced\n<!-- progress:end -->\n')
        self.assertEqual(git(self.clone, 'status', '--porcelain', '--untracked-files=no'), '')
        self.assertEqual(len(git(self.clone, 'rev-list', '--parents', '-n1', 'HEAD').split()), 3)

    def test_other_conflicts_are_left_for_you(self):
        self.write(self.origin, 'notes.txt', 'main\n')
        git(self.origin, 'add', 'notes.txt')
        git(self.origin, 'commit', '-q', '-m', 'main notes')
        self.write(self.clone, 'notes.txt', 'feature\n')
        git(self.clone, 'add', 'notes.txt')
        git(self.clone, 'commit', '-q', '-m', 'feature notes')
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'remerge.py'), '--no-build'],
                           cwd=self.clone, capture_output=True, text=True)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertIn('notes.txt', r.stderr)


if __name__ == '__main__':
    unittest.main()
