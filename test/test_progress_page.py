# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/progress_page.py: the progress page and its JSON, from the committed tables alone and stable."""
import html.parser
import json
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import progress  # noqa: E402
import progress_page  # noqa: E402
from test_progress_map import synthetic_tree, write  # noqa: E402

VOID = {'meta', 'img', 'br', 'hr', 'link', 'input'}


class Balance(html.parser.HTMLParser):
    """Checks that every element is closed in order (void elements excepted)."""

    def __init__(self):
        super().__init__()
        self.stack: list[str] = []
        self.errors: list[str] = []
        self.ids: list[str] = []

    def handle_starttag(self, tag, attrs):
        self.ids += [v for k, v in attrs if k == 'id']
        if tag not in VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if not self.stack or self.stack[-1] != tag:
            self.errors.append(f'</{tag}> closes {self.stack[-1:] or "nothing"}')
        else:
            self.stack.pop()


def check_html(test: unittest.TestCase, page: str) -> None:
    test.assertTrue(page.startswith('<!DOCTYPE html>\n<html lang="en">'))
    p = Balance()
    p.feed(page)
    p.close()
    test.assertEqual(p.errors, [])
    test.assertEqual(p.stack, [])
    test.assertEqual(len(p.ids), len(set(p.ids)), 'duplicate ids')


def synthetic_runtime(root: str) -> None:
    write(root, 'symbols/imports.csv', 'importer,library,symbol,kind,provider\n'
          'eboot,libA,a1,function,runtime\neboot,libA,a2,function,\n'
          'eboot,libB,b1,function,mod.elf\nmod.elf,libA,a3,function,runtime\n')
    write(root, 'symbols/boot.json', json.dumps({
        'imports_total': 3, 'imports_bundled': 1, 'imports_implemented': 1, 'imports_unresolved': 0,
        'milestone': 'loaded', 'milestone_index': 0, 'milestones': ['loaded', 'next'], 'outcome': 'stop'}))


PRS = [
    {'number': 7, 'title': 'adds two <and> verifies one', 'author': 'someone', 'draft': False, 'error': '',
     'functions': [{'address': '0x00405000', 'status': 'replaced'},   # original on main: replaced now
                   {'address': '0x00403000', 'status': 'verified'},   # replaced on main: verified now
                   {'address': '0x00401000', 'status': 'replaced'}]},  # verified on main: no gain
    {'number': 3, 'title': 'same function again', 'author': 'other', 'draft': True, 'error': '',
     'functions': [{'address': '0x00405000', 'status': 'edge-verified'}]},
]


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        synthetic_tree(self.root)
        synthetic_runtime(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_deterministic_valid_and_complete(self):
        out = progress_page.render_all(self.root, PRS, 'abc123', 'https://example.invalid/r')
        self.assertEqual(set(out), {progress_page.PAGE, progress_page.DATA})
        self.assertEqual(out, progress_page.render_all(self.root, PRS, 'abc123', 'https://example.invalid/r'))
        page = out[progress_page.PAGE]
        check_html(self, page)
        for heading in ('<h1>Progress</h1>', 'Decompilation by library', 'By game system and source file',
                        'Verified functions by evidence', 'Function families', 'Runtime: system imports by library',
                        'Potential progress with open pull requests', 'href="progress.json"', 'src="progress.svg"',
                        'https://example.invalid/r/blob/main/STATUS.md', 'https://example.invalid/r/pull/7'):
            self.assertIn(heading, page)
        self.assertIn('adds two &lt;and&gt; verifies one', page)
        json.loads(out[progress_page.DATA])

    def test_totals_agree_with_progress(self):
        d = progress_page.compute(self.root)
        p = progress.compute(self.root)
        for k in ('total', 'replaced', 'verified'):
            self.assertEqual(d[k], p[k])
        self.assertEqual(sum(e['functions'] for e in d['libraries']), p['total']['functions'])
        self.assertEqual(sum(e['verified_bytes'] for e in d['libraries']), p['verified']['bytes'])
        self.assertEqual(d['source_files'], [{'file': 'Common/a.cpp', 'library': 'havok', 'functions': 2,
                                              'bytes': 2500, 'replaced': 1, 'verified': 0}])
        self.assertEqual(d['evidence']['inputs recorded in the game'], 1)

    def test_imports_by_library(self):
        imp = progress_page.compute(self.root)['imports']
        self.assertEqual(imp['executable'], [
            {'name': 'libA', 'imports': 2, 'runtime': 1, 'module': 0, 'missing': 1},
            {'name': 'libB', 'imports': 1, 'runtime': 0, 'module': 1, 'missing': 0}])
        self.assertEqual([e['name'] for e in imp['importers']], ['eboot', 'mod.elf'])

    def test_open_prs_count_only_raised_status_once(self):
        op = progress_page.compute(self.root, PRS)['open_prs']
        self.assertEqual((op['replaced'], op['verified']), (1, 1))
        self.assertEqual([p['number'] for p in op['prs']], [3, 7])
        self.assertEqual({p['number']: (p['replaced'], p['verified']) for p in op['prs']}, {3: (1, 0), 7: (1, 1)})
        self.assertEqual(op['libraries'], {'': {'replaced': 1, 'verified': 0}, 'havok': {'replaced': 0, 'verified': 1}})

    def test_without_open_prs_the_section_says_so(self):
        out = progress_page.render_all(self.root)
        self.assertNotIn('open_prs', json.loads(out[progress_page.DATA]))
        self.assertIn('Not computed for this build', out[progress_page.PAGE])

    def test_table_rows_parse_function_and_member_tables(self):
        self.assertEqual(progress_page.table_rows('address,size,name,system,status,notes\n'
                                                  '0x0040ABC0,5,f,s,verified,"a, b"\nbad,1,g,s,verified,\n'
                                                  '0x00400000,1,h,s,original,\n'),
                         [{'address': '0x0040abc0', 'status': 'verified'}])
        self.assertEqual(progress_page.table_rows('address,size,extent,value,status\n0x10,5,16,0x10,replaced\n'),
                         [{'address': '0x10', 'status': 'replaced'}])
        self.assertTrue(progress_page.TABLE_RE.match('symbols/families/reads_qword_field.csv'))
        self.assertFalse(progress_page.TABLE_RE.match('symbols/families/reads_qword_field.verification.csv'))

    def test_no_output_without_the_export(self):
        with tempfile.TemporaryDirectory() as empty:
            self.assertEqual(progress_page.render_all(empty), {})


class MainTables(unittest.TestCase):
    def test_generates_from_mains_tables_and_agrees_with_progress(self):
        out = progress_page.render_all(ROOT)
        check_html(self, out[progress_page.PAGE])
        d = json.loads(out[progress_page.DATA])
        p = progress.compute(ROOT)
        self.assertEqual((d['total'], d['replaced'], d['verified']), (p['total'], p['replaced'], p['verified']))
        self.assertEqual(sum(e['functions'] for e in d['libraries']), p['total']['functions'])
        self.assertEqual(sum(e['replaced'] for e in d['libraries']), p['replaced']['functions'])
        self.assertEqual(sum(e['verified'] for e in d['libraries']), p['verified']['functions'])
        if p['runtime']:
            exe = d['imports']['executable']
            self.assertEqual(sum(e['imports'] for e in exe), p['runtime']['imports_total'])
            self.assertEqual(sum(e['runtime'] for e in exe), p['runtime']['imports_implemented'])
            self.assertEqual(sum(e['module'] for e in exe), p['runtime']['imports_bundled'])
        self.assertEqual(out, progress_page.render_all(ROOT))
        self.assertLess(len(out[progress_page.PAGE].encode()), 1_000_000)


if __name__ == '__main__':
    unittest.main()
