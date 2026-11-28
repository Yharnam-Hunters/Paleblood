# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/speed_history.py: the README's speed chart, from docs/speed_history.csv alone and stable."""
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import speed_history  # noqa: E402

SVG = '{http://www.w3.org/2000/svg}'
SHA_A, SHA_B = 'a' * 40, 'b' * 40


def row(pr: int, avg: float, low, sha: str = SHA_A, before=None) -> dict:
    return {'date': '2026-11-20', 'pr': pr, 'sha': sha, 'avg': avg, 'low': low, 'before': before, 'source': 'PR body'}


class Render(unittest.TestCase):
    def test_svg_is_well_formed_with_a_point_per_row(self):
        svg = speed_history.render([row(1, 10.0, 5.0, before=9.0), row(2, 12.0, None, SHA_B)])
        root = ET.fromstring(svg)
        self.assertEqual(root.get('role'), 'img')
        self.assertIn('10.00 fps after #1 to 12.00 fps after #2', root.get('aria-label'))
        circles = root.findall(f'.//{SVG}circle')
        self.assertEqual(len(circles), 3 + 2)  # two averages and one low, plus two legend dots
        self.assertEqual(len(root.findall(f'.//{SVG}polyline')), 1)  # the average line; one low point has no line

    def test_missing_low_leaves_a_gap_not_a_zero(self):
        svg = speed_history.render([row(1, 10.0, 5.0), row(2, 11.0, None, SHA_B), row(3, 12.0, 6.0, 'c' * 40)])
        self.assertNotIn('#2 (2026-11-20): 0', svg)
        self.assertNotIn('<polyline points="209.3,224.0', svg)  # the two low points are apart, so no low line joins them
        self.assertEqual(svg.count('fill="#dfb317"'), 2 + 1)  # two low points and the legend dot

    def test_axis_top_rounds_up(self):
        self.assertEqual(speed_history.axis_top([row(1, 21.01, 5.0)]), 25.0)
        self.assertEqual(speed_history.axis_top([row(1, 20.0, 5.0)]), 25.0)

    def test_bad_rows_are_refused(self):
        for bad in ([row(1, 10.0, 5.0), row(1, 11.0, 5.0)],
                    [row(1, 10.0, 5.0, sha='abc')],
                    [row(1, 10.0, 12.0)],
                    [row(1, 0.0, None)],
                    [dict(row(1, 10.0, 5.0), source='')]):
            with self.assertRaises(ValueError):
                speed_history.render(bad)

    def test_same_table_gives_same_bytes(self):
        rows = [row(1, 10.0, 5.0), row(2, 12.0, 6.0, SHA_B)]
        self.assertEqual(speed_history.render(rows), speed_history.render(rows))


class CommittedTable(unittest.TestCase):
    def test_table_reads_checks_and_renders(self):
        rows = speed_history.read_rows(ROOT)
        self.assertGreaterEqual(len(rows), 2)
        speed_history.check_rows(rows)
        self.assertEqual(rows, sorted(rows, key=lambda r: r['date']))
        ET.fromstring(speed_history.render(rows))

    def test_main_writes_the_svg(self):
        with tempfile.TemporaryDirectory() as out:
            sys.argv = ['speed_history.py', '--out-dir', out]
            self.assertEqual(speed_history.main(), 0)
            self.assertTrue(os.path.getsize(os.path.join(out, speed_history.OUTPUT)) > 0)


if __name__ == '__main__':
    unittest.main()
