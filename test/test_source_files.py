# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/source_files.py on synthetic layouts: classification, the contiguity and home-cluster
checks, range propagation, the committed-table check, and an end-to-end run on a synthetic
executable. The mutant test shows the propagation cases catch wrong fills."""
import csv
import os
import struct
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'tools'))
sys.path.insert(0, HERE)
import ps4elf  # noqa: E402
import source_files as sf  # noqa: E402

RULES = [
    ('pattern', 'library', 'prefix'),
    (r'/alpha/src/(?P<file>.+)', 'alpha', 'Alpha/'),
    (r'^(?:\.\./)*(?P<file>Beta/.+)', 'beta', ''),
]
FUNC = 0x10  # every synthetic function is 16 bytes


def layout(n: int, start: int = 0x500000) -> list[tuple[int, int]]:
    return [(start + FUNC * i, FUNC) for i in range(n)]


def at(functions, i: int) -> int:
    return functions[i][0]


def write_rules(path: str) -> None:
    with open(path, 'w', newline='') as f:
        csv.writer(f, lineterminator='\n').writerows(RULES)


def rules():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'rules.csv')
        write_rules(p)
        return sf.read_rules(p)


A = ('alpha', 'Alpha/a.cpp')
A2 = ('alpha', 'Alpha/b.cpp')
B = ('beta', 'Beta/x.cpp')


def propagation_cases(module) -> list[str]:
    """Failures of `module`'s propagate() on the reference cases (empty when it is right)."""
    fns = layout(12)
    direct = {at(fns, 0): A, at(fns, 3): A, at(fns, 5): A2, at(fns, 7): B, at(fns, 9): B}
    got = module.propagate(fns, direct)
    want = [(*A, 'direct'), (*A, 'range'), (*A, 'range'), (*A, 'direct'),
            ('alpha', '', 'range'), (*A2, 'direct'), None, (*B, 'direct'), (*B, 'range'),
            (*B, 'direct'), None, None]
    return [f'function {i}: {g} != {w}' for i, (g, w) in enumerate(zip(got, want)) if g != w]


class Classify(unittest.TestCase):
    def test_paths(self):
        r = rules()
        self.assertEqual(sf.classify(r'Q:\build\alpha\src\core\a.cpp', r), ('alpha', 'Alpha/core/a.cpp'))
        self.assertEqual(sf.classify('../../Beta/One/../Two/x.cpp', r), ('beta', 'Beta/Two/x.cpp'))
        self.assertIsNone(sf.classify(r'Q:\build\alpha\src\core\a.inl', r), 'headers are inlined elsewhere')
        self.assertIsNone(sf.classify(r'Q:\build\gamma\g.cpp', r), 'no rule, no tag')


class Anchors(unittest.TestCase):
    def test_rarest_candidate_wins(self):
        candidates = {1: {A, B}, 2: {B}, 3: {B}}
        self.assertEqual(sf.choose_files(candidates), {1: A, 2: B, 3: B})

    def test_sandwiched_run_is_dropped(self):
        tags = {1: A, 2: A, 3: B, 4: A}
        kept, dropped = sf.drop_inlined(tags)
        self.assertEqual((kept, dropped), ({1: A, 2: A, 4: A}, [3]))

    def test_longer_run_inside_is_kept(self):
        tags = {1: A, 2: B, 3: B, 4: A}
        self.assertEqual(sf.drop_inlined(tags), (tags, []))

    def test_alternation_keeps_the_middle(self):
        tags = {1: A, 2: B, 3: A, 4: B, 5: A}
        self.assertEqual(sf.drop_inlined(tags), ({1: A, 3: A, 5: A}, [2, 4]))

    def test_home_cluster(self):
        far = sf.HOME_GAP + 1
        tags = {0x1000: A, 0x1000 + far: A, 0x1010 + far: A, 0x1020 + far: B}
        kept, dropped = sf.keep_home(tags)
        self.assertEqual(dropped, [0x1000])
        self.assertEqual(kept, {0x1000 + far: A, 0x1010 + far: A, 0x1020 + far: B})


class Propagate(unittest.TestCase):
    def test_reference_cases(self):
        self.assertEqual(propagation_cases(sf), [])

    def test_mutants_fail(self):
        """Each deliberately wrong propagation must fail the reference cases."""
        original = sf.propagate

        def fills_across_libraries(functions, direct):
            out = original(functions, direct)
            return [t if t is not None or i in (10, 11) else ('alpha', '', 'range') for i, t in enumerate(out)]

        def fills_file_between_files(functions, direct):
            return [(t[0], t[1] or 'Alpha/a.cpp', t[2]) if t else None for t in original(functions, direct)]

        def no_ranges(functions, direct):
            return [t if t is None or t[2] == 'direct' else None for t in original(functions, direct)]

        def tags_the_tail(functions, direct):
            out = original(functions, direct)
            out[-1] = out[-3]
            return out

        for mutant in (fills_across_libraries, fills_file_between_files, no_ranges, tags_the_tail):
            with self.subTest(mutant=mutant.__name__), mock.patch.object(sf, 'propagate', mutant):
                self.assertNotEqual(propagation_cases(sf), [], f'{mutant.__name__} was not caught')

    def test_rows_and_confidence(self):
        fns = layout(5)
        rows = sf.to_rows(fns, sf.propagate(fns, {at(fns, 0): A, at(fns, 2): A, at(fns, 4): A2}))
        self.assertEqual([(r['address'], r['last'], r['functions'], r['bytes'], r['evidence'], r['confidence'])
                          for r in rows],
                         [('0x00500000', '0x00500000', 1, 16, 'direct', 'high'),
                          ('0x00500010', '0x00500010', 1, 16, 'range', 'medium'),
                          ('0x00500020', '0x00500020', 1, 16, 'direct', 'high'),
                          ('0x00500030', '0x00500030', 1, 16, 'range', 'low'),
                          ('0x00500040', '0x00500040', 1, 16, 'direct', 'high')])


class EndToEnd(unittest.TestCase):
    """A synthetic executable: eight functions, three of which load source paths."""

    def build(self, root: str) -> str:
        base = sf.BASE
        strings_at = 0x200
        strings = (b'Q:\\build\\alpha\\src\\a.cpp\0' + b'Q:\\build\\alpha\\src\\a.h\0' + b'../Beta/x.cpp\0')
        offsets = {'a.cpp': 0, 'a.h': strings.index(b'Q:\\build\\alpha\\src\\a.h'), 'x.cpp': strings.index(b'../Beta')}
        loads = {0: 'a.cpp', 1: 'a.h', 3: 'a.cpp', 6: 'x.cpp'}
        code = b''
        for i in range(8):
            body = b''
            if i in loads:
                disp = strings_at + offsets[loads[i]] - (FUNC * i + 7)
                body = b'\x48\x8d\x35' + struct.pack('<i', disp)
            code += (body + b'\xc3').ljust(FUNC, b'\xcc')
        segment = code.ljust(strings_at, b'\0') + strings
        elf = os.path.join(root, 'test.elf')
        with open(elf, 'wb') as f:
            f.write(ps4elf.build([(0, segment, len(segment), 5)], [], []))
        os.makedirs(os.path.join(root, 'symbols'))
        os.makedirs(os.path.join(root, 'game'))
        write_rules(os.path.join(root, sf.RULES))
        with open(os.path.join(root, sf.FUNCTIONS), 'w') as f:
            f.write('address,size\n' + ''.join(f'0x{base + FUNC * i:08x},{FUNC}\n' for i in range(8)))
        return elf

    def test_generate_and_check(self):
        with tempfile.TemporaryDirectory() as root:
            elf = self.build(root)
            text = sf.generate(root, elf)
            rows = list(csv.DictReader(text.splitlines()))
            self.assertEqual([(r['address'], r['last'], r['library'], r['file'], r['evidence']) for r in rows],
                             [('0x00400000', '0x00400000', 'alpha', 'Alpha/a.cpp', 'direct'),
                              ('0x00400010', '0x00400020', 'alpha', 'Alpha/a.cpp', 'range'),
                              ('0x00400030', '0x00400030', 'alpha', 'Alpha/a.cpp', 'direct'),
                              ('0x00400060', '0x00400060', 'beta', 'Beta/x.cpp', 'direct')])
            table = os.path.join(root, sf.TABLE)
            with open(table, 'w') as f:
                f.write(text)
            self.assertEqual(sf.check(root, None), [])
            self.assertEqual(sf.check(root, elf), [])
            os.makedirs(os.path.join(root, 'docs'))
            with open(os.path.join(root, sf.DOC), 'w') as f:
                f.write('# Catalog\n<!-- source-files:start -->\n<!-- source-files:end -->\n')
            self.assertNotEqual(sf.check(root, None), [], 'an empty coverage block is stale')
            self.assertEqual(sf.update_doc(root, text, write=True), [])
            self.assertEqual(sf.check(root, None), [])
            with open(os.path.join(root, sf.DOC)) as f:
                self.assertIn('| alpha | 4 | 64 | 2 | 2 | 0 |', f.read())
            with open(table, 'w') as f:
                f.write(text.replace('Alpha/a.cpp,range', 'Alpha/z.cpp,range'))
            self.assertNotEqual(sf.check(root, None), [], 'an edited range row is stale')
            with open(table, 'w') as f:
                f.write(text.replace('0x00400060,0x00400060', '0x00400050,0x00400060'))
            self.assertNotEqual(sf.check(root, None), [], 'a widened direct row no longer matches its counts')
            self.assertNotEqual(sf.check(root, elf), [])


if __name__ == '__main__':
    unittest.main()
