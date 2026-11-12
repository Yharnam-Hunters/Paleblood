# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/match.py: the matching record's format check, the comparison rules, and the matching
sources' syntax. Everything here is synthetic: no dump, no compiler of the era."""
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import match  # noqa: E402
import verification  # noqa: E402

CALL = 0xe8


class Record(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.root, 'game', 'matching', 'x'))
        os.makedirs(os.path.join(self.root, 'symbols'))
        self.source = 'game/matching/x/f.c'
        with open(os.path.join(self.root, self.source), 'w') as f:
            f.write('#include "f.h"\nint x_f(int a) { return a + 1; }\n')
        with open(os.path.join(self.root, 'game', 'matching', 'x', 'f.h'), 'w') as f:
            f.write('int x_f(int a);\n')
        self.funcs = [{'address': '0x00401000', 'name': 'x_f', 'status': 'edge-verified', '_line': 2}]

    def tearDown(self):
        shutil.rmtree(self.root)

    def row(self, **change):
        r = {'address': '0x00401000', 'name': 'x_f', 'compiler': 'clang-3.4.2', 'flags': 'ps4-o2',
             'match_percent': '100.0', 'objdiff_percent': '', 'byte_exact': 'yes', 'source': self.source,
             'source_sha256': match.source_hash(self.root, self.source), 'date': '2026-11-12'}
        r.update(change)
        return r

    def write(self, *rows):
        match.write_rows(os.path.join(self.root, match.MATCHING_CSV), list(rows))

    def errors(self):
        return match.check(self.root, self.funcs)

    def test_good_row(self):
        self.write(self.row())
        self.assertEqual(self.errors(), [])

    def test_no_table_is_fine(self):
        self.assertEqual(self.errors(), [])

    def test_header(self):
        with open(os.path.join(self.root, match.MATCHING_CSV), 'w') as f:
            f.write('address,name\n')
        self.assertTrue(any('header' in e for e in self.errors()))

    def test_changed_source_or_header_is_stale(self):
        self.write(self.row())
        with open(os.path.join(self.root, 'game', 'matching', 'x', 'f.h'), 'a') as f:
            f.write('/* changed */\n')
        self.assertTrue(any('changed since it was matched' in e for e in self.errors()))

    def test_rejects(self):
        cases = {
            'name': self.row(name='x_g'),
            'compiler': self.row(compiler='clang-3.5'),
            'flags': self.row(flags='O3'),
            'match_percent': self.row(match_percent='100'),
            'byte_exact': self.row(byte_exact='no'),
            'objdiff_percent': self.row(objdiff_percent='abc'),
            'under game/': self.row(source='tools/f.c'),
            'does not exist': self.row(source='game/matching/x/missing.c'),
            'date': self.row(date='12/11/2026'),
            'no row in symbols/functions.csv': self.row(address='0x00402000'),
        }
        for needle, r in cases.items():
            with self.subTest(needle):
                self.write(r)
                self.assertTrue(any(needle in e for e in self.errors()), self.errors())

    def test_source_must_define_the_function(self):
        with open(os.path.join(self.root, 'game', 'matching', 'x', 'g.c'), 'w') as f:
            f.write('int other(void) { return 0; }\n')
        self.write(self.row(source='game/matching/x/g.c', source_sha256=match.source_hash(self.root, 'game/matching/x/g.c')))
        self.assertTrue(any('does not define x_f' in e for e in self.errors()))

    def test_byte_exact_stands_in_for_recordings_only(self):
        verified = [{'name': 'x_f', 'status': 'verified', '_line': 2}]
        verification.write_csv(os.path.join(self.root, 'symbols', 'verification.csv'),
                               verification.VERIFICATION_HEADER,
                               [{'function': 'x_f', 'cases': 'edge', 'option': '', 'total': '2', 'passed': '2',
                                 'failed': '0', 'quarantined': '0', 'source': '', 'date': '2026-11-12'}])
        self.assertTrue(any('current recorded result' in e for e in verification.check(self.root, verified)))
        self.write(self.row())
        self.assertEqual(verification.check(self.root, verified), [])
        self.write(self.row(byte_exact='no', match_percent='97.0'))
        self.assertTrue(any('current recorded result' in e for e in verification.check(self.root, verified)))


class Comparison(unittest.TestCase):
    def test_relocation_fields_are_masked(self):
        original = bytes([0x55, CALL]) + struct.pack('<i', 0x1234) + bytes([0x5d, 0xc3])
        ours = bytes([0x55, CALL]) + bytes(4) + bytes([0x5d, 0xc3])
        self.assertTrue(match.masked_equal(original, ours, {2: 4}))
        self.assertFalse(match.masked_equal(original, ours, {}))
        self.assertFalse(match.masked_equal(original, ours[:-1], {2: 4}))

    def test_got_load_relaxed_to_lea(self):
        original = bytes([0x48, match.LEA, 0x05]) + struct.pack('<i', 0x100) + bytes([0xc3])
        ours = bytes([0x48, match.MOV_LOAD, 0x05]) + bytes(4) + bytes([0xc3])
        self.assertTrue(match.masked_equal(original, ours, {3: match.R_X86_64_GOTPCREL}))
        # only a GOT relocation allows it: a plain PC-relative field keeps the opcode exact
        self.assertFalse(match.masked_equal(original, ours, {3: 2}))

    def test_external_references_and_normalize(self):
        start, size = 0x1000, 16
        insns = [(0x1000, bytes([0x55]), 'push   rbp'),
                 (0x1001, bytes([CALL]) + struct.pack('<i', 0x5000 - 0x1006), 'call   0x5000'),
                 (0x1006, bytes([0x74, 0x02]), 'je     0x100a'),
                 (0x1008, bytes.fromhex('488b05') + struct.pack('<i', 0x10), 'mov    rax,QWORD PTR [rip+0x10]        # 0x101f')]
        refs = match.external_references(insns, start, start + size)
        self.assertEqual(refs, {2: 0x5000, 11: 0x101f})
        self.assertEqual(match.normalize(insns, start, set(refs)),
                         ['push rbp', 'call EXT', 'je L+10', 'GOTADDR rax'])


class Sources(unittest.TestCase):
    """Matching sources compile with a current C compiler too (CI cannot run clang 3.4.2)."""

    def test_c_sources_parse(self):
        compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        sources = sorted(glob.glob(os.path.join(ROOT, 'game', 'matching', '**', '*.c'), recursive=True))
        if not compiler or not sources:
            self.skipTest('no C compiler or no matching sources')
        for path in sources:
            with self.subTest(os.path.relpath(path, ROOT)):
                r = subprocess.run([compiler, '-std=gnu99', '-fsyntax-only', '-Wall', '-Werror', path],
                                   capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()
