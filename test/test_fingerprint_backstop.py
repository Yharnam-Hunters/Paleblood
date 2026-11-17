# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/fingerprint_backstop.py: a fingerprint row whose legacy hash moved stays current only where
the compiled code of what its function reaches is unchanged (docs/VERIFY.md, "Source fingerprints").

A hole in the fingerprint is planted by fixing it to one value, so the fingerprint sees no change at
all: whatever the backstop then catches, it catches without the fingerprint's help."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import fingerprint_backstop  # noqa: E402
import verification  # noqa: E402

HEADER = '''\
#pragma once
#include <stdint.h>

struct Thing {
    int32_t count;
};

inline int32_t scaled(const Thing *t) { return t->count * 4; }
inline int32_t unused_helper(int32_t v) { return v + 1; }
'''

SOURCE = '''\
#include "sys.h"

static const char *const g_names[] = {"first", "second"};

extern "C" int32_t bb_sys_target(Thing *t)
{
    return scaled(t) + g_names[t->count & 1][0];
}

extern "C" int32_t bb_sys_neighbour(int32_t v)
{
    return unused_helper(v);
}
'''

OTHER = '''\
#include <stdint.h>

extern "C" int32_t bb_other_value(void) { return 3; }
'''

HEADER_ROW = ','.join(verification.VERIFICATION_HEADER)
BLIND = 'f1:' + '0' * 16       # the planted hole: the fingerprint never changes


def git(cwd, *args):
    return subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.invalid',
                           '-c', 'commit.gpgsign=false', *args],
                          cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


@unittest.skipUnless(shutil.which('git') and shutil.which(os.environ.get('CXX') or 'c++'),
                     'needs git and a C++ compiler')
class BackstopTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        self.write('game/sys/sys.h', HEADER)
        self.write('game/sys/target.cpp', SOURCE)
        self.write('game/other/value.cpp', OTHER)
        self.write('symbols/verification.csv', HEADER_ROW + '\n')
        git(self.root, 'init', '-q', '-b', 'main')
        self.commit('base')
        self.blind = mock.patch.object(verification, 'source_fingerprint',
                                       lambda root, name, family=False: BLIND)
        self.blind.start()

    def tearDown(self):
        self.blind.stop()
        self.tmp.cleanup()

    def write(self, rel, text):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            f.write(text)

    def edit(self, rel, old, new):
        with open(os.path.join(self.root, rel)) as f:
            text = f.read()
        assert old in text, old
        self.write(rel, text.replace(old, new, 1))

    def commit(self, message):
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '--allow-empty', '-m', message)
        return git(self.root, 'rev-parse', 'HEAD')

    def record(self, name='sys_target', value=None):
        value = value or verification.record_value(self.root, name)
        self.write('symbols/verification.csv', f'{HEADER_ROW}\n{name},edge,,4,4,0,0,{value},2026-11-17\n')
        return value

    def check(self, base):
        return fingerprint_backstop.check(self.root, base, jobs=2, log=lambda *a: None)

    def test_the_row_carries_the_legacy_hash(self):
        value = self.record()
        self.assertEqual(value, f"{BLIND}:{verification.source_hash(self.root, 'sys_target')}")

    def test_unrelated_edit_passes(self):
        self.record()
        base = self.commit('record')
        self.edit('game/sys/sys.h', 'return v + 1;', 'return v + 2;')
        self.edit('game/sys/target.cpp', 'return unused_helper(v);', 'return unused_helper(v) * 7;')
        self.commit('unrelated')
        self.assertEqual(self.check(base), [])

    def test_planted_hole_in_a_header_is_caught(self):
        self.record()
        base = self.commit('record')
        self.edit('game/sys/sys.h', 'return t->count * 4;', 'return t->count * 5;')
        self.commit('changes what the function computes')
        errs = self.check(base)
        self.assertEqual(len(errs), 1, errs)
        self.assertIn('compiled code of bb_sys_target', errs[0])
        self.assertIn('symbols/verification.csv:2', errs[0])

    def test_planted_hole_in_reached_data_is_caught(self):
        self.record()
        base = self.commit('record')
        self.edit('game/sys/target.cpp', '"second"', '"Second"')
        self.commit('changes a string the function reads')
        self.assertEqual(len(self.check(base)), 1)

    def test_planted_hole_in_a_struct_layout_is_caught(self):
        self.record()
        base = self.commit('record')
        self.edit('game/sys/sys.h', 'int32_t count;', 'int32_t pad;\n    int32_t count;')
        self.commit('moves the field')
        self.assertEqual(len(self.check(base)), 1)

    def test_a_file_that_does_not_compile_is_stale(self):
        self.record()
        base = self.commit('record')
        self.edit('game/sys/target.cpp', 'return unused_helper(v);', 'return unused_helper(v)')
        self.commit('broken')
        errs = self.check(base)
        self.assertEqual(len(errs), 1, errs)
        self.assertIn('treated as stale', errs[0])
        self.assertIn('does not compile', errs[0])

    def test_untouched_files_need_no_compiler(self):
        self.record()
        self.commit('record')
        self.edit('game/sys/sys.h', 'return v + 1;', 'return v + 2;')
        base = self.commit('unrelated, checked when it landed')
        self.edit('game/other/value.cpp', 'return 3;', 'return 4;')
        self.commit('another system')
        with mock.patch.object(fingerprint_backstop, 'compile_object', side_effect=AssertionError('compiled')):
            self.assertEqual(self.check(base), [])

    def test_row_added_on_the_branch_is_compared_with_its_commit(self):
        base = self.commit('nothing yet')
        self.edit('game/sys/sys.h', 'return t->count * 4;', 'return t->count * 6;')
        self.record()
        self.commit('code and its record')
        self.edit('game/sys/sys.h', 'return v + 1;', 'return v + 2;')
        self.commit('unrelated')
        self.assertEqual(self.check(base), [])
        self.edit('game/sys/sys.h', 'return t->count * 6;', 'return t->count * 4;')
        self.commit('back to the base code, which the record was not made on')
        errs = self.check(base)
        self.assertEqual(len(errs), 1, errs)
        self.assertIn('compiled code', errs[0])

    def test_row_recorded_on_uncommitted_code_is_stale(self):
        base = self.commit('nothing yet')
        self.edit('game/sys/sys.h', 'return t->count * 4;', 'return t->count * 9;')
        value = verification.record_value(self.root, 'sys_target')
        self.edit('game/sys/sys.h', 'return t->count * 9;', 'return t->count * 4;')
        self.record(value=value)
        self.commit('a record of code that was never committed')
        errs = self.check(base)
        self.assertEqual(len(errs), 1, errs)
        self.assertIn('no commit since', errs[0])

    def test_rerecording_makes_it_current(self):
        self.record()
        base = self.commit('record')
        self.edit('game/sys/sys.h', 'return t->count * 4;', 'return t->count * 5;')
        self.record()
        self.commit('changed and recorded again')
        self.assertEqual(self.check(base), [])

    def test_legacy_rows_are_left_to_the_legacy_rule(self):
        self.record(value=verification.source_hash(self.root, 'sys_target'))
        base = self.commit('legacy record')
        self.edit('game/sys/sys.h', 'return t->count * 4;', 'return t->count * 5;')
        self.commit('change')
        self.assertEqual(self.check(base), [])
        self.assertFalse(verification.row_current(self.root, 'sys_target',
                                                  verification.read_csv(os.path.join(self.root, 'symbols', 'verification.csv'),
                                                                        verification.VERIFICATION_HEADER)[0]['source']))

    def test_a_fingerprint_without_its_legacy_hash_is_never_current(self):
        self.assertFalse(verification.row_current(self.root, 'sys_target', BLIND))


class ObjectReadingTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which(os.environ.get('CC') or 'cc'), 'needs a C compiler')
    def test_reaches_called_code_and_not_its_neighbours(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'game', 'sys'))
            src = 'static int helper(int v) { return v + %d; }\nint bb_sys_a(int v) { return helper(v); }\n' \
                  'int bb_sys_b(int v) { return v * %d; }\n'

            def code(a, b):
                with open(os.path.join(root, 'game', 'sys', 'a.c'), 'w') as f:
                    f.write('__attribute__((noinline)) ' + src % (a, b))
                out = os.path.join(root, 'a.o')
                self.assertIsNone(fingerprint_backstop.compile_object(root, 'game/sys/a.c', out))
                with open(out, 'rb') as f:
                    obj = fingerprint_backstop.ObjectFile(f.read())
                return fingerprint_backstop.reached_code(obj, lambda n: n == 'bb_sys_a')

            first = code(1, 2)
            self.assertIsNotNone(first)
            self.assertEqual(code(1, 3), first)         # bb_sys_b is not reached
            self.assertNotEqual(code(2, 2), first)      # the helper is


if __name__ == '__main__':
    unittest.main()
