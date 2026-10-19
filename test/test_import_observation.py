# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic boundary snapshots are diagnostics, never host instruction coverage."""
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import instruction_trace as trace  # noqa: E402
import verify  # noqa: E402
import differential  # noqa: E402


class ImportObservation(unittest.TestCase):
    def begin(self, regions):
        registers = trace.Registers()
        registers.rsp = 0x1000
        registers.rsi = 0x2001  # Raw bits, not an inferred pointer declaration.
        with patch.object(trace, '_read_memory', side_effect=lambda _l, _p, _a, size: bytes(size)):
            observation, pending = trace._begin_import_observation(
                None, 42, registers, 0x1010, regions, 'syntheticImport',
                'guest:0x400010', 'original')
        return registers, observation, pending

    def test_snapshot_budget_and_stack_window_are_explicit(self):
        regions = [{'start': 0x1000, 'end': 0x3000, 'name': 'host_stack', 'read': True, 'write': True},
                   {'start': 0x4000, 'end': 0x8000, 'name': 'buf:large', 'read': True, 'write': True}]
        _, observation, pending = self.begin(regions)
        self.assertEqual(sum(size for _, size, _ in pending), trace.MAX_IMPORT_SNAPSHOT_BYTES)
        self.assertEqual(observation['omitted_bytes'], 0x4000 + 24 - trace.MAX_IMPORT_SNAPSHOT_BYTES)
        self.assertTrue(observation['memory'][-1]['truncated'])
        registers = trace.Registers()
        registers.rsp = 0x1000
        with patch.object(trace, '_read_memory', side_effect=lambda _l, _p, _a, size: bytes(size)):
            observation, _ = trace._begin_import_observation(
                None, 42, registers, 0x1ff8, regions, 'syntheticImport', 'guest:0x400010', 'original')
        self.assertEqual(observation['memory'][0]['size'], trace.MAX_IMPORT_STACK_BYTES)
        self.assertTrue(observation['memory'][0]['truncated'])

    def test_unreadable_bytes_are_unknown_not_zero_or_unchanged(self):
        registers, observation, pending = self.begin(
            [{'start': 0x2000, 'end': 0x2008, 'name': 'buf:out', 'read': True, 'write': True}])
        self.assertEqual(observation['stack_snapshot_status'], 'unavailable')
        self.assertIn('outside declared readable regions', observation['stack_snapshot_reason'])
        with patch.object(trace, '_read_memory', return_value=None):
            trace._finish_import_observation(None, 42, registers, observation, pending)
        self.assertIsNone(observation['memory'][0]['after'])
        self.assertIsNone(observation['memory'][0]['changed_offsets'])
        self.assertFalse(observation['memory_access_coverage'])
        with patch.object(trace, '_read_memory', side_effect=[b'x' * 64, None]):
            self.assertIsNone(trace._snapshot_memory(None, 42, 0x2000, 65))

    def test_unaligned_changed_bytes_are_not_reported_as_memory_accesses(self):
        registers, observation, pending = self.begin(
            [{'start': 0x2001, 'end': 0x2005, 'name': 'buf:unaligned', 'read': True, 'write': True}])
        with patch.object(trace, '_read_memory', return_value=bytes.fromhex('0000ff00')):
            trace._finish_import_observation(None, 42, registers, observation, pending)
        self.assertEqual(observation['memory'][0]['changed_offsets'], [2])
        self.assertEqual(observation['registers_before']['rsi'], '0x2001')
        self.assertFalse(observation['instruction_coverage'])
        self.assertFalse(observation['cfg_feedback_eligible'])

    def test_compare_rejects_forged_completeness_for_each_external_evidence_channel(self):
        for evidence in ({'skipped_host_calls': 1}, {'external_transitions': [{}]},
                         {'import_observations': [{'status': 'returned'}]}):
            with self.subTest(evidence=evidence):
                result = {'instruction_trace': {'complete': True, **evidence}}
                differences = verify.compare_case(result, result)
                self.assertTrue(any('untraced external execution' in item for item in differences))
                result['instruction_trace']['control_flow_coverage'] = {
                    'status': 'complete', 'feedback_eligible': True, 'blocks': [{'address': 'guest:0x400000'}],
                    'edges': []}
                coverage = differential.summarize_control_flow_coverage({'original': [result]})['by_side']['original']
                self.assertEqual(coverage['feedback_fixture_count'], 0)
                self.assertEqual(coverage['fixture_coverage']['unknown']['status'], 'partial')
