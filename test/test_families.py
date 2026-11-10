# SPDX-License-Identifier: GPL-2.0-or-later
"""Function families (docs/FAMILIES.md): shapes, the scanner's gates, the tables' validation,
the generated hook table and the session report's family evidence. Synthetic data only."""
import csv
import os
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import families as fam  # noqa: E402
import family_emit  # noqa: E402
import family_scan  # noqa: E402
import family_verify  # noqa: E402
import gen_hooks  # noqa: E402
import session_report  # noqa: E402
import validate_functions  # noqa: E402

PADDING_15 = bytes.fromhex('6666666666662e0f1f840000000000')


def write_csv(path, header, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(rows)


class Shapes(unittest.TestCase):
    def test_fixed_shape_matches_only_its_exact_bytes(self):
        shape = fam.parse_shape('31 c0 c3')
        self.assertTrue(shape.matches(bytes.fromhex('31c0c3')))
        self.assertFalse(shape.matches(bytes.fromhex('31c0c390')))
        self.assertFalse(shape.matches(bytes.fromhex('c0c3')))
        self.assertIsNone(shape.value(bytes.fromhex('31c0c3')))

    def test_parameter_fields(self):
        self.assertEqual(fam.parse_shape('b8 {u32} c3').value(bytes.fromhex('b8d0000000c3')), 0xd0)
        disp = fam.parse_shape('48 8b 47 {d8} c3')
        self.assertEqual(disp.value(bytes.fromhex('488b4740c3')), 0x40)
        for negative_or_zero in ('488b47f8c3', '488b4700c3'):
            with self.assertRaises(fam.ShapeError):
                disp.value(bytes.fromhex(negative_or_zero))

    def test_bad_shapes_are_refused(self):
        for text in ('', 'C3', 'b8 {u32} {u8}', 'zz', 'b8 {u16}'):
            with self.assertRaises(fam.ShapeError):
                fam.parse_shape(text)

    def test_names(self):
        family = {'family': 'returns_constant', 'system': 'leaf'}
        self.assertEqual(fam.member_name(family, '0x00401230'), 'leaf_returns_constant_00401230')
        self.assertEqual(fam.replacement(family, '0x1'), 'bb_leaf_returns_constant_0x1')
        self.assertEqual(fam.replacement(family, ''), 'bb_leaf_returns_constant')


class FakeImage:
    """Code at 0x401000.. as one buffer, addressed like the repository."""
    def __init__(self, code: bytes):
        self.data = code

    def offset(self, address):
        off = address - 0x401000
        return off if 0 <= off < len(self.data) else None


def scanner(code: bytes, functions, named=(), relocated=(), in_source=(), patched=()):
    s = object.__new__(family_scan.Scanner)
    s.image = FakeImage(code)
    s.functions = list(functions)
    s.named = set(named)
    s.relocated = sorted(relocated)
    s.in_source = set(in_source)
    s.patched = list(patched)
    s.libraries = []
    return s


class ScannerGates(unittest.TestCase):
    SHAPE = fam.parse_shape('c3')

    def layout(self, padding=PADDING_15):
        code = b'\xc3' + padding + b'\xc3' + PADDING_15 + b'\xcc' * 16
        return code, [(0x401000, 1), (0x401010, 1), (0x401020, 16)]

    def test_member_with_padding_to_the_next_function(self):
        code, functions = self.layout()
        reason, row = scanner(code, functions).classify(0, self.SHAPE)
        self.assertEqual(reason, 'member')
        self.assertEqual((row['address'], row['size'], row['extent'], row['value']), ('0x00401000', '1', '16', ''))

    def test_code_in_the_padding_excludes(self):
        code, functions = self.layout(bytes.fromhex('9090909090e90b0000009090909090'))
        self.assertEqual(scanner(code, functions).classify(0, self.SHAPE)[0], 'code after it before the next function')

    def test_each_gate_excludes(self):
        code, functions = self.layout()
        self.assertEqual(scanner(code, functions, named={0x401000}).classify(0, self.SHAPE)[0],
                         'already in symbols/functions.csv')
        self.assertEqual(scanner(code, functions, relocated={0x40100c}).classify(0, self.SHAPE)[0],
                         'relocation in its extent')
        self.assertEqual(scanner(code, functions, in_source={0x401000}).classify(0, self.SHAPE)[0],
                         'named by a game/ source file')
        self.assertEqual(scanner(code, functions, patched=[(0x40100f, 0x401010)]).classify(0, self.SHAPE)[0],
                         'community patch line in its extent')
        self.assertEqual(scanner(code, functions).classify(2, self.SHAPE)[0], 'shape')

    def test_short_extent_excludes(self):
        code = b'\xc3' + b'\x90' * 7 + b'\xc3' + PADDING_15
        reason, _ = scanner(code, [(0x401000, 1), (0x401008, 1), (0x401018, 1)]).classify(0, self.SHAPE)
        self.assertEqual(reason, 'extent under 14 bytes')

    def test_relocation_slot_reaching_into_the_extent(self):
        code, functions = self.layout()
        # an 8-byte slot starting 7 bytes before the member still overlaps its first byte
        self.assertEqual(scanner(code, functions, relocated={0x400ff9}).classify(0, self.SHAPE)[0],
                         'relocation in its extent')
        self.assertEqual(scanner(code, functions, relocated={0x400ff8}).classify(0, self.SHAPE)[0], 'member')


class Tables(unittest.TestCase):
    """validate_functions.check_families on a synthetic repository."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.d, 'game', 'leaf'))
        with open(os.path.join(self.d, 'game', 'leaf', 'returns_zero.cpp'), 'w') as f:
            f.write('#include <cstdint>\nextern "C" uint64_t bb_leaf_returns_zero(void) { return 0; }\n')
        self.export = [{'address': '0x00401000', 'size': '3'}, {'address': '0x00401010', 'size': '3'},
                       {'address': '0x00401020', 'size': '40'}]
        self.family(['returns_zero', 'leaf', '31 c0 c3', 'i64', '2', '6', 'xor eax, eax; ret'])
        self.members([['0x00401000', '3', '16', '', 'replaced'], ['0x00401010', '3', '16', '', 'replaced']])

    def tearDown(self):
        shutil.rmtree(self.d)

    def family(self, row):
        write_csv(os.path.join(self.d, 'symbols', 'families.csv'), fam.FAMILY_HEADER, [row])

    def members(self, rows):
        write_csv(os.path.join(self.d, 'symbols', 'families', 'returns_zero.csv'), fam.MEMBER_HEADER, rows)

    def errors(self, named=(), hooked=()):
        return validate_functions.check_families(self.d, self.export, set(named), set(hooked), ['leaf'])

    def test_valid_tables(self):
        self.assertEqual(self.errors(), [])

    def test_counts_must_match_the_pre_registration(self):
        self.family(['returns_zero', 'leaf', '31 c0 c3', 'i64', '3', '9', 'xor eax, eax; ret'])
        self.assertTrue(any('registers 3' in e for e in self.errors()))

    def test_extent_must_reach_the_next_function(self):
        self.members([['0x00401000', '3', '14', '', 'replaced'], ['0x00401010', '3', '16', '', 'replaced']])
        self.assertTrue(any('does not reach the next function' in e for e in self.errors()))

    def test_member_cannot_also_be_named_or_hooked(self):
        self.assertTrue(any('functions.csv' in e for e in self.errors(named={'0x00401000'})))
        self.assertTrue(any('game/hooks.csv' in e for e in self.errors(hooked={'0x00401010'})))

    def test_replacement_must_exist_with_the_declared_return(self):
        self.family(['returns_zero', 'leaf', '31 c0 c3', 'void', '2', '6', 'xor eax, eax; ret'])
        self.assertTrue(any("returns 'uint64_t'" in e for e in self.errors()))
        os.remove(os.path.join(self.d, 'game', 'leaf', 'returns_zero.cpp'))
        self.assertTrue(any('no extern "C" definition' in e for e in self.errors()))

    def test_edge_verified_needs_a_current_passing_result(self):
        self.members([['0x00401000', '3', '16', '', 'edge-verified'], ['0x00401010', '3', '16', '', 'replaced']])
        self.assertTrue(any('without a current, passing edge result' in e for e in self.errors()))
        source = fam.source_hash(self.d, {'family': 'returns_zero', 'system': 'leaf'})
        write_csv(os.path.join(self.d, 'symbols', 'families', 'returns_zero.verification.csv'),
                  ['function', 'cases', 'option', 'total', 'passed', 'failed', 'quarantined', 'source', 'date'],
                  [['leaf_returns_zero_00401000', 'edge', '', '3', '3', '0', '0', source, '2026-11-10']])
        self.assertEqual(self.errors(), [])
        with open(os.path.join(self.d, 'game', 'leaf', 'returns_zero.cpp'), 'a') as f:
            f.write('// changed\n')
        self.assertTrue(any('without a current, passing edge result' in e for e in self.errors()))

    def test_duplicate_results_and_source_named_members_are_refused(self):
        source = fam.source_hash(self.d, {'family': 'returns_zero', 'system': 'leaf'})
        row = ['leaf_returns_zero_00401000', 'edge', '', '3', '3', '0', '0', source, '2026-11-10']
        write_csv(os.path.join(self.d, 'symbols', 'families', 'returns_zero.verification.csv'),
                  ['function', 'cases', 'option', 'total', 'passed', 'failed', 'quarantined', 'source', 'date'],
                  [row, row])
        self.assertTrue(any('more than one edge row' in e for e in self.errors()))
        os.remove(os.path.join(self.d, 'symbols', 'families', 'returns_zero.verification.csv'))
        with open(os.path.join(self.d, 'game', 'leaf', 'observed.c'), 'w') as f:
            f.write('/* observed at 0x00401010u */\n')
        self.assertTrue(any('names 0x00401010' in e for e in self.errors()))

    def test_value_must_fit_the_shape(self):
        self.family(['returns_zero', 'leaf', '31 c0 c3', 'i64', '2', '6', 'xor eax, eax; ret'])
        self.members([['0x00401000', '3', '16', '0x1', 'replaced'], ['0x00401010', '3', '16', '', 'replaced']])
        self.assertTrue(any('value must be empty' in e for e in self.errors()))

    def test_unknown_table_in_the_directory(self):
        write_csv(os.path.join(self.d, 'symbols', 'families', 'stray.csv'), fam.MEMBER_HEADER, [])
        self.assertTrue(any('stray.csv' in e for e in self.errors()))

    def test_hook_table(self):
        lines = gen_hooks.family_table(self.d)
        self.assertIn('void bb_leaf_returns_zero(void);', lines)
        self.assertIn('    { 0x00401000u, 16u, "returns_zero", (rt_hook_fn)bb_leaf_returns_zero },', lines)
        self.assertIn('const unsigned game_family_hook_count = 2u;', lines)


class Emitter(unittest.TestCase):
    def test_one_replacement_per_value_and_the_validator_compares(self):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d)
        os.makedirs(os.path.join(d, 'game', 'leaf'))
        write_csv(os.path.join(d, 'symbols', 'families.csv'), fam.FAMILY_HEADER,
                  [['returns_constant', 'leaf', 'b8 {u32} c3', 'i64', '3', '18', 'mov eax, imm32; ret']])
        write_csv(os.path.join(d, 'symbols', 'families', 'returns_constant.csv'), fam.MEMBER_HEADER,
                  [['0x00401000', '6', '16', '0x2', 'replaced'], ['0x00401010', '6', '16', '0x10', 'replaced'],
                   ['0x00401020', '6', '16', '0x2', 'replaced']])
        text = family_emit.render(d, 'returns_constant')
        self.assertEqual(text.count('extern "C" uint64_t bb_leaf_returns_constant_'), 2)
        self.assertLess(text.index('_0x2(void)'), text.index('_0x10(void)'))
        export = [{'address': f'0x{0x401000 + 16 * i:08x}', 'size': '6'} for i in range(4)]
        errs = validate_functions.check_families(d, export, set(), set(), ['leaf'])
        self.assertTrue(any("differs from tools/family_emit.py's output" in e for e in errs))
        with open(os.path.join(d, 'game', 'leaf', 'returns_constant.cpp'), 'w') as f:
            f.write(text)
        self.assertEqual(validate_functions.check_families(d, export, set(), set(), ['leaf']), [])


class Evidence(unittest.TestCase):
    CHANGE = [{'family': 'returns_zero', 'shape': '31 c0 c3', 'members': 2, 'bytes': 6, 'replaced': 2,
               'edge-verified': 2, 'verified': 0}]

    def verify(self, **override):
        v = {'function': 'family:returns_zero', 'family': 'returns_zero', 'members': 2, 'cases': 6,
             'passed': 6, 'failed': 0, 'mutants': [{'mutant': 'bb_mutant_returns_one', 'members': 2, 'killed': 2}]}
        v.update(override)
        return v

    def test_clean_evidence(self):
        d = {'families': self.CHANGE, 'verify': [self.verify()]}
        self.assertEqual(session_report.family_evidence_errors(d, self.CHANGE), [])

    def test_surviving_mutant_or_failure_or_missing_evidence(self):
        for bad in (self.verify(mutants=[{'mutant': 'm', 'members': 2, 'killed': 1}]), self.verify(mutants=[]),
                    self.verify(failed=1, passed=5), self.verify(members=1)):
            self.assertTrue(session_report.family_evidence_errors({'families': self.CHANGE, 'verify': [bad]}, None))
        self.assertTrue(session_report.family_evidence_errors({'families': self.CHANGE, 'verify': []}, None))

    def test_report_must_list_every_changed_family(self):
        errs = session_report.family_evidence_errors({'families': [], 'verify': []}, self.CHANGE)
        self.assertTrue(any('omits changed families' in e for e in errs))

    def test_family_changes(self):
        families = {'returns_zero': {'shape': '31 c0 c3', 'bytes': '6'}}
        before = {'returns_zero': {'0x00401000': 'replaced'}}
        after = {'returns_zero': {'0x00401000': 'edge-verified', '0x00401010': 'edge-verified'}}
        [change] = session_report.family_changes(families, before, after)
        self.assertEqual((change['replaced'], change['edge-verified'], change['verified']), (1, 2, 0))
        self.assertEqual(session_report.family_changes(families, after, after), [])


class Sampling(unittest.TestCase):
    def test_sample_covers_every_value(self):
        members = [{'address': f'0x{0x401000 + 16 * i:08x}', 'value': '0x8' if i < 99 else '0x10'} for i in range(100)]
        picked = family_verify.sample_members(members, 10)
        self.assertEqual({m['value'] for m in picked}, {'0x8', '0x10'})
        self.assertLessEqual(len(picked), 12)

    def test_sibling(self):
        family = {'family': 'reads_qword_field', 'system': 'leaf'}
        self.assertEqual(family_verify.sibling_symbol(family, ['0x8', '0x10'], '0x8'), 'bb_leaf_reads_qword_field_0x10')
        self.assertEqual(family_verify.sibling_symbol(family, ['0x8', '0x10'], '0x10'), 'bb_leaf_reads_qword_field_0x8')
        self.assertIsNone(family_verify.sibling_symbol(family, ['0x8'], '0x8'))
        self.assertIsNone(family_verify.sibling_symbol(family, [], ''))


if __name__ == '__main__':
    unittest.main()
