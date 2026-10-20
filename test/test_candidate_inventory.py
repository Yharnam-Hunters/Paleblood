# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic preflight contracts only; these are not recorded game inputs or verification."""
import copy
import json
import sqlite3
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import automation_pipeline as pipeline
import candidate_inventory as inventory
import differential
import function_catalog as catalog
import function_qualification as qualification


class CandidateInventory(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / 'symbols').mkdir()
        (self.root / 'symbols/ghidra_functions.csv').write_text('address,size\n0x00400100,3\n')
        (self.root / 'symbols/functions.csv').write_text('address,name,system,status\n0x00400100,candidate,unit,original\n')
        self.elf = self.root / 'synthetic-identity'
        self.elf.write_text('synthetic identity only; this test does not execute a target ELF')
        self.sha = catalog.sha256(self.elf)
        self.state = self.root / 'state.sqlite3'
        rows, edges, metadata = catalog.build_rows(self.root, executable_sha256=self.sha)
        self.function, self.metadata = rows[0], metadata
        catalog.write_snapshot(self.state, rows, edges, metadata)
        now = int(time.time())
        self.snapshot = {'snapshot_complete': True, 'snapshot_sha256': 'synthetic-owner', 'checked_at': now, 'claims': [],
                         'sources': [{'source': key, 'status': 'complete', 'checked_at': now}
                                     for key in sorted(qualification.REQUIRED_OWNERSHIP_SOURCES)]}
        qualification.cache_snapshot(self.state, self.snapshot)
        self.decision = {'address': self.function['address'], 'qualification_cache_key': 'synthetic',
                         'qualification_version': qualification.QUALIFICATION_VERSION,
                         'catalog_identity': metadata['catalog_identity'], 'ownership_snapshot_sha256': 'synthetic-owner',
                         'qualification_tools': qualification.qualification_tool_identity(ROOT),
                         'classification': 'eligible_for_replacement_development',
                         'eligible_for_replacement_development': True, 'ownership': {}}
        qualification.save_qualifications(self.state, [self.decision])
        self.captures = self.root / 'candidate'
        self.captures.mkdir()
        self.abi = {'calling_convention': 'sysv_amd64', 'confidence': 'supported',
                    'evidence': ['https://example.org/synthetic-abi'], 'stack_alignment': 16, 'arguments': [],
                    'returns': {'type': 'uint64_t'}, 'input_regions': [], 'writable_regions': [],
                    'execution_limits': {'timeout_seconds': 1}}
        self.case = {'schema': 1, 'address': self.function['address'], 'id': 'input-id', 'returns': 'i64',
                     'args': {}, 'buffers': {}, 'memory': [], 'imports': [], 'stubs': [], 'abi': self.abi,
                     'capture': {'function': 'candidate', 'run_id': 'synthetic-unit-test',
                                 'executable_sha256': self.sha, 'recorded_at': 'synthetic'}}
        self.case_path = self.captures / 'fixture.json'
        self.case_path.write_text(json.dumps(self.case))

    def tearDown(self):
        self.temporary.cleanup()

    def owner(self):
        return qualification.classify_ownership(self.function['address'], 'candidate', self.snapshot)

    def bundle(self):
        fixture_sha = differential.digest_bytes(catalog.sha256(self.case_path).encode())
        return {'identity': {'address': self.function['address'], 'executable_sha256': self.sha,
                             'fixture_sha256': fixture_sha, 'abi_sha256': inventory.digest(self.abi),
                             **inventory.current_tools(ROOT)},
                'result': {'kind': 'original_only', 'execution': {'cases': [{'id': 'fixture',
                    'instruction_trace': {'instructions': 2, 'complete': True,
                        'events': [{'access': 'read', 'width': 8, 'valid_region': True, 'before': '00' * 8}],
                        'control_flow_coverage': {'status': 'complete', 'feedback_eligible': True,
                                                  'blocks': [{'address': 'guest:0x400100'}], 'edges': []}}}]}}}

    def report(self, bundle=None, owner=None, directory=None):
        return inventory.inspect(self.function, self.captures if directory is None else directory,
                                 self.metadata, owner or self.owner(), self.decision, minimum=1, trace_bundle=bundle)

    def test_fresh_ownership_contract_reaches_pipeline_for_canonical_original_status(self):
        candidate = differential.catalog_candidate(self.state, self.function['address'])
        self.assertEqual(candidate['ownership']['status'], 'confirmed_unclaimed')
        self.assertTrue(candidate['qualification']['eligible_for_replacement_development'])
        bundle = self.bundle()
        decision = candidate['qualification']
        current = inventory.inspect(self.function, self.captures, self.metadata, candidate['ownership'], decision,
                                    minimum=1, trace_bundle=bundle)
        self.assertTrue(current['automation_trial_eligible'])
        report_path, trace_path, abi_path = (self.root / name for name in ('report.json', 'trace.json', 'abi.json'))
        report_path.write_text(json.dumps({'rows': [current]}))
        trace_path.write_text(json.dumps(bundle))
        abi_path.write_text(json.dumps(self.abi))
        args = SimpleNamespace(output=self.root / 'outputs', worktree_root=self.root / 'worktrees', base='synthetic',
                               abi=abi_path, address=self.function['address'], state=self.state, name='candidate',
                               regression_only=False, patch=Path('synthetic.patch'), elf=self.elf,
                               captures=self.captures, minimum_fixtures=1, eligibility_report=report_path,
                               trace_evidence=trace_path)
        result = pipeline.Runner(args, {}, 'synthetic').stage_eligibility()
        self.assertEqual(result['kind'], 'replacement_candidate')

    def test_missing_stale_future_and_conflicting_ownership_never_qualify(self):
        for mutate in ('missing', 'stale', 'future'):
            snapshot = copy.deepcopy(self.snapshot)
            if mutate == 'missing': snapshot['sources'].pop()
            if mutate == 'stale': snapshot['sources'][0]['checked_at'] -= qualification.OWNERSHIP_MAX_AGE_SECONDS + 1
            if mutate == 'future': snapshot['sources'][0]['checked_at'] += 3600
            owner = qualification.classify_ownership(self.function['address'], 'candidate', snapshot)
            self.assertEqual(owner['status'], 'unknown')
            self.assertFalse(self.report(self.bundle(), owner)['automation_trial_eligible'])
        self.snapshot['claims'] = [{'address': self.function['address'], 'kind': 'contributor', 'owner': 'one'},
                                   {'address': self.function['address'], 'kind': 'contributor', 'owner': 'two'}]
        self.assertEqual(self.owner()['status'], 'ownership_conflict')
        self.assertFalse(self.report(self.bundle())['automation_trial_eligible'])
        qualification.cache_snapshot(self.state, self.snapshot)
        command = [sys.executable, str(ROOT / 'tools/function_catalog.py'), '--state', str(self.state), '--json']
        status = json.loads(subprocess.run(command + ['status'], check=True, capture_output=True, text=True).stdout)
        self.assertEqual(status['ownership_classification_counts'], {'ownership_conflict': 1})
        ranked = json.loads(subprocess.run(command + ['rank'], check=True, capture_output=True, text=True).stdout)
        self.assertEqual(ranked, [])

    def test_fixture_identity_generated_inputs_and_unavailable_directory_are_distinct(self):
        for field, value in (('executable_sha256', 'b' * 64), ('run_id', ''), ('function', 'other')):
            changed = copy.deepcopy(self.case)
            changed['capture'][field] = value
            self.case_path.write_text(json.dumps(changed))
            report = self.report(self.bundle())
            self.assertFalse(report['automation_trial_eligible'])
            self.assertEqual(report['recorded_fixture_count'], 0)
        changed = copy.deepcopy(self.case)
        changed['exploration'] = {'seed': 1}
        self.assertTrue(inventory.provenance_blockers([changed], 'candidate', self.sha))
        report = self.report(directory=self.root / 'absent')
        self.assertIsNone(report['root_fixture_count'])
        self.assertEqual(report['instruction_trace']['status'], 'unavailable')

    def test_incomplete_external_and_stale_trace_evidence_cannot_qualify(self):
        self.assertFalse(self.report()['automation_trial_eligible'])
        for flag in ('skipped_host_calls', 'external_transitions', 'import_observations'):
            bundle = self.bundle()
            bundle['result']['execution']['cases'][0]['instruction_trace'][flag] = 1
            self.assertEqual(self.report(bundle)['instruction_trace']['status'], 'incomplete')
        bundle = self.bundle()
        bundle['identity']['harness_sha256'] = 'old'
        self.assertEqual(self.report(bundle)['instruction_trace']['status'], 'stale')
        bundle = self.bundle()
        bundle['result']['execution']['cases'][0]['instruction_trace']['events'] = [{'width': 1, 'valid_region': False}]
        self.assertFalse(self.report(bundle)['automation_trial_eligible'])
        for malformed in (None, {'cases': [{'id': []}]}, {'cases': 'unavailable'}):
            bundle = self.bundle()
            bundle['result']['execution'] = malformed
            self.assertFalse(self.report(bundle)['automation_trial_eligible'])
        self.case['capture']['run_id'] = []
        self.case_path.write_text(json.dumps(self.case))
        self.assertFalse(self.report(self.bundle())['automation_trial_eligible'])

    def test_reports_are_idempotent_and_changed_fixtures_or_forged_flags_are_rejected(self):
        report = self.report(self.bundle())
        inventory.save_reports(self.state, [report])
        inventory.save_reports(self.state, [report])
        database = catalog.connect(self.state)
        self.assertEqual(database.execute('SELECT count(*) FROM candidate_inventories').fetchone()[0], 1)
        database.close()
        with self.assertRaises(sqlite3.IntegrityError):
            inventory.save_reports(self.state, [{**report, 'report_identity': 'interrupted'},
                                                {**report, 'report_identity': None}])
        database = catalog.connect(self.state)
        self.assertEqual(database.execute('SELECT identity FROM candidate_inventories').fetchone()[0], report['report_identity'])
        database.close()
        inventory.require_current_report(report, self.report(self.bundle()))
        self.case['args'] = {'rdi': '0x1'}
        self.case_path.write_text(json.dumps(self.case))
        with self.assertRaisesRegex(ValueError, 'stale'):
            inventory.require_current_report(report, self.report(self.bundle()))
        rejected = self.report()
        forged = {**rejected, 'automation_trial_eligible': True}
        with self.assertRaisesRegex(ValueError, 'blocked'):
            inventory.require_current_report(forged, rejected)

    def test_cli_reports_no_paths_or_fixture_bodies_and_rechecks_qualification(self):
        command = [sys.executable, str(ROOT / 'tools/function_catalog.py'), '--state', str(self.state), '--json',
                   'trial-inventory', '--captures', str(self.captures), '--minimum-recorded', '1']
        process = subprocess.run(command, capture_output=True, text=True, check=True)
        report = json.loads(process.stdout)
        self.assertEqual(report['automation_trial_eligible'], 0)  # No execution evidence supplied.
        self.assertNotIn(str(self.root), process.stdout)
        self.assertNotIn('synthetic-unit-test', process.stdout)
        self.assertEqual(report['rows'][0]['root_fixture_count'], 1)

    def test_original_runner_normalizes_batch_results_preserves_faults_and_trace_flags(self):
        (self.captures / 'abi.json').write_text(json.dumps(self.abi))
        rows, abi = differential.load_fixtures(self.captures, self.function['address'])
        self.assertEqual(len(rows), 1)
        self.assertEqual(abi, self.abi)
        response = SimpleNamespace(returncode=0, stdout=json.dumps({'fixture': {'fault': 'synthetic fault'}}))
        with patch.object(differential.subprocess, 'run', return_value=response) as command:
            result = differential.run_original(self.elf, self.captures, self.function['address'], 1, True, 42)
        self.assertEqual(result['cases'][0]['fault'], 'synthetic fault')
        self.assertIn('--trace-instructions', command.call_args.args[0])
        self.assertIn('42', command.call_args.args[0])
