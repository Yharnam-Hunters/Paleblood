# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic metadata only; no target layouts or function behavior established here."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import function_catalog as catalog
import function_intelligence as intelligence


def artifact():
    source = {'source': 'synthetic metadata', 'confidence': 'structural_candidate'}
    return {'schema': 1, 'executable_sha256': 'a' * 64, 'analysis_version': 'synthetic 1',
            'types': [{'id': 'A', 'address': '0x3000', 'name': 'UnitDerived', 'encoded_name': None,
                       'bases_status': 'bounded_abi_layout_candidate',
                       'bases': [{'type_id': 'B', 'offset_flags': 2, **source},
                                 {'type_id': 'C', 'offset_flags': 4098, **source}], **source},
                      {'id': 'B', 'address': '0x3100', 'name': None, 'encoded_name': None, 'bases': [], **source}],
            'vtables': [{'address': '0x4000', 'type_id': 'A', 'offset_to_top': 0,
                         'slots': [{'index': 0, 'function': '0x1000'}, {'index': 1, 'function': '0x1010'}],
                         'extent': 'bounded_prefix_unknown_full_extent', **source},
                        {'address': '0x4100', 'type_id': 'A', 'offset_to_top': -16,
                         'slots': [{'index': 0, 'function': '0x1000'}],
                         'extent': 'bounded_prefix_unknown_full_extent', **source}],
            'relations': [{'function': '0x1020', 'type_id': 'A', 'vtable': '0x4000',
                           'role': 'vtable_reference_candidate', 'reference': '0x1021', **source}],
            'functions': [], 'scan': {'truncated': True, 'limitations': ['synthetic bounded prefix']}}


class FunctionIntelligence(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / 'symbols').mkdir()
        (self.root / 'symbols/ghidra_functions.csv').write_text('address,size\n0x1000,4\n0x1010,4\n0x1020,4\n')
        (self.root / 'symbols/functions.csv').write_text('address,name,system,status\n')
        self.state = self.root / 'catalog.sqlite3'
        self.functions, self.edges, self.metadata = catalog.build_rows(self.root, executable_sha256='a' * 64)
        catalog.write_snapshot(self.state, self.functions, self.edges, self.metadata)
        self.path = self.root / 'intelligence.json'
        self.path.write_text(json.dumps(artifact()))

    def tearDown(self):
        self.temporary.cleanup()

    def test_multiple_inheritance_and_vtable_aliases_preserve_evidence_not_layout_proof(self):
        raw = intelligence.normalize(artifact(), 'a' * 64)
        rows, summary = intelligence.annotate(self.functions, raw, [])
        self.assertEqual(len(raw['types'][0]['bases']), 2)
        self.assertEqual(len(raw['vtables']), 2)
        self.assertEqual(summary['functions_with_type_associations'], 3)
        self.assertEqual(rows[0]['classification']['category'], 'unknown')
        self.assertEqual({r['vtable'] for r in rows[0]['type_intelligence']['relations']}, {'0x00004000', '0x00004100'})
        self.assertFalse(rows[0]['classification']['behavior_established'])
        self.assertEqual(raw['types'][1]['base_discovery'], ['unknown'])

    def test_duplicate_types_merge_and_ambiguous_names_remain_unresolved(self):
        raw = artifact()
        raw['types'].append(copy.deepcopy(raw['types'][0]))
        normalized = intelligence.normalize(raw, 'a' * 64)
        self.assertEqual(len(normalized['types']), 2)
        self.assertFalse(normalized['types'][0]['ambiguous'])
        raw['types'][-1]['name'] = 'ConflictingName'
        normalized = intelligence.normalize(raw, 'a' * 64)
        rows, _ = intelligence.annotate(self.functions, normalized, [])
        self.assertTrue(normalized['types'][0]['ambiguous'])
        self.assertEqual(rows[0]['type_intelligence']['names'], [])
        self.assertTrue(rows[0]['classification']['conflict'])

    def test_missing_rtti_is_unknown_and_wrong_identity_or_instruction_bytes_are_rejected(self):
        raw = artifact()
        raw['types'] = []
        rows, summary = intelligence.annotate(self.functions, intelligence.normalize(raw, 'a' * 64), [])
        self.assertEqual(summary['classes_with_names'], 0)
        self.assertEqual(summary['functions_with_type_associations'], 0)
        self.assertEqual(rows[0]['classification']['category'], 'unknown')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            intelligence.normalize(raw, 'b' * 64)
        raw['instruction_bytes'] = 'not accepted'
        with self.assertRaisesRegex(ValueError, 'bytes and listings'):
            intelligence.normalize(raw, 'a' * 64)

    def test_conflicting_symbol_classifications_do_not_select_a_winner(self):
        raw = artifact()
        raw['functions'] = [{'address': '0x1000', 'name': 'std::unit', 'thunk': False,
                             'source': 'synthetic symbol', 'confidence': 'symbol_supported'},
                            {'address': '0x1000', 'name': 'unit_thunk', 'thunk': True,
                             'source': 'synthetic analysis', 'confidence': 'analysis_supported'}]
        rows, summary = intelligence.annotate(self.functions, intelligence.normalize(raw, 'a' * 64), [])
        self.assertTrue(rows[0]['classification']['conflict'])
        self.assertEqual(summary['classification']['unknown']['functions'], 3)
        self.assertEqual(sum(c['functions'] for c in summary['classification'].values()), 3)

    def test_community_mapping_requires_provenance_and_never_proves_target_layout(self):
        mapping = {'type_name': 'UnitDerived', 'label': 'possible actor family', 'category': 'game_engine',
                   'source_url': 'https://example.org/research', 'license': 'Apache-2.0', 'revision': 'commit123',
                   'attribution': 'synthetic researcher', 'source_game': 'another title'}
        path = self.root / 'mappings.json'
        path.write_text(json.dumps({'schema': 1, 'mappings': [mapping]}))
        hints = intelligence.load_mappings(path)
        rows, _ = intelligence.annotate(self.functions, intelligence.normalize(artifact(), 'a' * 64), hints)
        self.assertEqual(rows[0]['classification']['category'], 'unknown')
        self.assertEqual(rows[0]['classification']['candidates'][0]['confidence'], 'candidate')
        self.assertFalse(hints[0]['target_layout_or_behavior_proven'])
        for license_name in ('unknown', 'NOASSERTION', 'MIT-NON-AI'):
            mapping['license'] = license_name
            path.write_text(json.dumps({'schema': 1, 'mappings': [mapping]}))
            with self.assertRaisesRegex(ValueError, 'unlicensed'):
                intelligence.load_mappings(path)

    def test_reimport_is_idempotent_and_interrupted_snapshot_rolls_back(self):
        first = intelligence.import_artifact(self.state, self.path)
        first_snapshot = catalog.load_snapshot(self.state)
        self.assertEqual(intelligence.import_artifact(self.state, self.path), first)
        self.assertEqual(catalog.load_snapshot(self.state), first_snapshot)
        saved = intelligence.load_artifact(self.state)
        functions, edges, metadata = first_snapshot
        with self.assertRaisesRegex(RuntimeError, 'interrupted'):
            catalog.write_snapshot(self.state, functions, edges, metadata, fail_after=1)
        self.assertEqual(intelligence.load_artifact(self.state), saved)
        self.assertEqual(catalog.load_snapshot(self.state), first_snapshot)

    def test_cli_and_ranking_context_do_not_establish_eligibility(self):
        command = [sys.executable, str(ROOT / 'tools/function_catalog.py'), '--state', str(self.state), '--json',
                   'intelligence']
        subprocess.run(command + ['import', str(self.path)], check=True, capture_output=True)
        query = subprocess.run(command + ['types', '--limit', '1'], check=True, capture_output=True, text=True)
        self.assertEqual(len(json.loads(query.stdout)), 1)
        functions, edges, _ = catalog.load_snapshot(self.state)
        ranked = catalog.rank_functions(functions, edges)
        self.assertEqual(ranked, catalog.rank_functions(functions, edges))
        self.assertTrue(all(item['eligibility'] == 'conditional' for item in ranked))
        self.assertIn('type_context', ranked[0]['component_scores'])
