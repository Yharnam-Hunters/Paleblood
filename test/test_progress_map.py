# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/progress_map.py: the README's progress map and badges, from symbols/ alone and stable."""
import datetime
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import progress  # noqa: E402
import progress_map  # noqa: E402

SVG = '{http://www.w3.org/2000/svg}'


def write(root: str, rel: str, text: str) -> None:
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)


def synthetic_tree(root: str) -> None:
    """Six functions in three libraries (one unattributed); two replaced, one of them verified."""
    write(root, 'symbols/ghidra_functions.csv', 'address,size\n'
          '0x00400000,1000\n0x00401000,3000\n0x00402000,2000\n0x00403000,500\n0x00404000,1500\n0x00405000,2000\n')
    write(root, 'symbols/source_files.csv', 'address,last,functions,bytes,library,file,evidence,confidence\n'
          '0x00400000,0x00401000,2,4000,engine,,range,low\n'
          '0x00402000,0x00403000,2,2500,havok,Common/a.cpp,direct,high\n')
    write(root, 'symbols/functions.csv', 'address,size,name,system,status,notes\n'
          '0x00401000,3000,sys_big,sys,verified,\n'
          '0x00403000,500,sys_small,sys,replaced,\n'
          '0x00405000,2000,sys_open,sys,original,\n')
    write(root, 'symbols/verification.csv', 'function,cases,option,total,passed,failed,quarantined,source,date\n'
          'sys_big,recorded,,10,10,0,0,aa,2026-11-01\n'
          'sys_small,edge,,5,5,0,0,bb,2026-11-09\n'
          'sys_big,edge,,5,5,0,0,aa,2026-11-10\n')
    write(root, 'symbols/matching.csv', 'address,name,compiler,flags,match_percent,objdiff_percent,byte_exact,'
          'targets_checked,source,source_sha256,date\n'
          '0x00401000,sys_big,clang,o2,100.0,100.0,yes,yes,x.c,00,2026-11-10\n')


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        synthetic_tree(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_libraries_by_address(self):
        rows = progress_map.status_rows(self.root)
        libs = progress_map.library_totals(self.root, rows)
        self.assertEqual(set(libs), {'engine', 'havok', ''})
        self.assertEqual((libs['engine']['bytes'], libs['engine']['verified_bytes']), (4000, 3000))
        self.assertEqual((libs['havok']['functions'], libs['havok']['replaced'], libs['havok']['verified']), (2, 1, 0))
        self.assertEqual((libs['']['functions'], libs['']['bytes'], libs['']['replaced']), (2, 3500, 0))

    def test_evidence_and_week(self):
        rows = progress_map.status_rows(self.root)
        self.assertEqual(progress_map.evidence(self.root, rows), {
            'inputs recorded in the game': 1, 'byte-exact matching source': 1, 'in a function family': 0})
        tested = progress_map.first_tested(self.root)
        self.assertEqual(tested['sys_big'], datetime.date(2026, 11, 1))
        # newest test date 2026-11-10: sys_small (first tested 11-09) is in the week, sys_big is not
        self.assertEqual(progress_map.replaced_in_week(rows, tested), 1)

    def test_outputs_are_deterministic_well_formed_and_small(self):
        out = progress_map.render_all(self.root)
        self.assertEqual(set(out), {progress_map.MAP, *progress_map.BADGES})
        self.assertEqual(out, progress_map.render_all(self.root))
        for name, svg in out.items():
            ET.fromstring(svg)  # well-formed XML
            self.assertLess(len(svg.encode()), 200_000, name)
        self.assertIn('Functions verified: 16.67% (1/6)', out[progress_map.MAP])
        self.assertIn('functions verified: 16.67% (1)', out['badge-verified.svg'])
        self.assertIn('+1 functions', out['badge-week.svg'])

    def test_every_block_has_a_tooltip(self):
        tree = ET.fromstring(progress_map.render_all(self.root)[progress_map.MAP])
        titles = [t.text for t in tree.iter(f'{SVG}title')]
        for name in ('FromSoftware', 'Havok', 'not attributed yet'):
            self.assertTrue(any(name in t for t in titles), name)

    def test_no_output_without_the_export(self):
        with tempfile.TemporaryDirectory() as empty:
            self.assertEqual(progress_map.render_all(empty), {})


class Layout(unittest.TestCase):
    def test_squarify_tiles_the_whole_area_in_proportion(self):
        sizes = [202, 79, 50, 35, 32, 6, 1]
        rects = progress_map.squarify(sizes, 10, 20, 600, 400)
        self.assertEqual(len(rects), len(sizes))
        for size, (x, y, w, h) in zip(sizes, rects):
            self.assertAlmostEqual(w * h, 600 * 400 * size / sum(sizes), places=3)
            self.assertTrue(10 - 1e-6 <= x and x + w <= 610 + 1e-6 and 20 - 1e-6 <= y and y + h <= 420 + 1e-6)

    def test_group_areas_below_their_titles_are_in_proportion(self):
        sizes, header = [208, 153, 64], 22
        regions = progress_map.group_regions(sizes, 0, 0, 976, 560, header, 10)
        inner = [w * (h - header) for _, _, w, h in regions]
        for a, s in zip(inner, sizes):
            self.assertAlmostEqual(a / sum(inner), s / sum(sizes), places=6)
        self.assertAlmostEqual(regions[-1][1] + regions[-1][3], 560, places=6)

    def test_cell_counts_round_but_never_hide_replaced_bytes(self):
        lib = {'bytes': 10_000, 'verified_bytes': 1, 'replaced_bytes': 2}
        self.assertEqual(progress_map.cell_counts(lib, 100), (1, 0))
        lib = {'bytes': 10_000, 'verified_bytes': 0, 'replaced_bytes': 2}
        self.assertEqual(progress_map.cell_counts(lib, 100), (0, 1))
        lib = {'bytes': 10_000, 'verified_bytes': 2500, 'replaced_bytes': 5000}
        self.assertEqual(progress_map.cell_counts(lib, 100), (25, 25))
        lib = {'bytes': 10_000, 'verified_bytes': 10_000, 'replaced_bytes': 10_000}
        self.assertEqual(progress_map.cell_counts(lib, 100), (100, 0))

    def test_cell_runs_cover_exactly_the_cells(self):
        for start, count, columns in ((0, 0, 7), (0, 5, 7), (3, 2, 7), (3, 20, 7), (7, 14, 7), (5, 30, 6)):
            cells = set()
            for col, row, w, h in progress_map.cell_runs(start, count, columns):
                self.assertLessEqual(col + w, columns)
                cells |= {(row + r) * columns + col + c for r in range(h) for c in range(w)}
            self.assertEqual(cells, set(range(start, start + count)))


class MainTables(unittest.TestCase):
    def test_generates_from_mains_tables_and_agrees_with_progress(self):
        out = progress_map.render_all(ROOT)
        data = progress.compute(ROOT)
        v, t = data['verified'], data['total']
        self.assertIn(f"({v['functions']:,}/{t['functions']:,})", out[progress_map.MAP])
        libs = progress_map.library_totals(ROOT, progress_map.status_rows(ROOT))
        self.assertEqual(sum(e['functions'] for e in libs.values()), t['functions'])
        self.assertEqual(sum(e['bytes'] for e in libs.values()), t['bytes'])
        self.assertEqual(sum(e['verified'] for e in libs.values()), v['functions'])
        for svg in out.values():
            ET.fromstring(svg)


if __name__ == '__main__':
    unittest.main()
