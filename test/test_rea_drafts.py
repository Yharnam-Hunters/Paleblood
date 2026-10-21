# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic MCP/ELF regressions for private REA draft export; no native engine required."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "test"))
import rea_drafts as drafts  # noqa: E402
from ps4elf import build  # noqa: E402


PEER = r'''
import hashlib, json, pathlib, sys
config_path = pathlib.Path(sys.argv[1])
retained = []
for line in sys.stdin:
    request = json.loads(line)
    config = json.loads(config_path.read_text())
    with open(config['log'], 'a') as stream:
        stream.write(json.dumps(request) + '\n')
    if 'id' not in request:
        continue
    method, params = request['method'], request.get('params', {})
    if method == 'initialize':
        result = {'protocolVersion': '2024-11-05', 'capabilities': {},
                  'serverInfo': {'name': 'synthetic-rea', 'version': '6.7.0'}}
    elif method == 'tools/list':
        names = ['open_binary', 'close_binary', 'list_procedures', 'procedure_info',
                 'address_to_file_offset', 'analyze_function', 'export_evidence_bundle']
        result = {'tools': [{'name': name, 'inputSchema': {'type': 'object'}} for name in names]}
    else:
        name, arguments = params['name'], params['arguments']
        image_hash = config['image_sha256']
        if name == 'open_binary':
            if config.get('stall_open'):
                continue  # Leave this request pending while still reading cancellation/EOF.
            content = {'result': {'sha256': image_hash}}
        elif name == 'close_binary':
            content = {'result': {'closed': True}}
        else:
            content = {'evidence_id': 'ev_' + hashlib.sha256((name + json.dumps(arguments, sort_keys=True)).encode()).hexdigest(),
                       'subject': {'digest': {'sha256': image_hash}},
                       'operation': name,
                       'analysis_profile': {'digest': config.get('profile', 'p' * 64)}}
            if name == 'list_procedures':
                value = [{'address': hex(address + config.get('analysis_base', 0))} for address in (0x1000, 0x1010)]
            elif name == 'procedure_info':
                value = {'entrypoint': '0xff0' if config.get('neighbor') else arguments['procedure']}
            elif name == 'address_to_file_offset':
                value = {'file_offset': int(arguments['address'], 0) - config.get('analysis_base', 0) + config.get('mapping_shift', 0)}
            elif name == 'analyze_function':
                value = {'pseudocode': '' if config.get('no_pseudocode') else 'synthetic placeholder',
                         'procedure': {'address': '0xff0' if config.get('wrong_dossier') else arguments['procedure']}}
            elif name == 'export_evidence_bundle':
                records = json.loads(json.dumps(retained))
                if config.get('bundle_neighbor') and records:
                    neighbor = json.loads(json.dumps(records[-1]))
                    neighbor['evidence_id'] = 'ev_' + 'f' * 64
                    neighbor['operation'] = 'other_operation'
                    neighbor['subject']['digest']['sha256'] = 'f' * 64
                    records.insert(0, neighbor)
                if config.get('bundle_wrong_target'):
                    for record in records:
                        record['subject']['digest']['sha256'] = 'e' * 64
                if config.get('bundle_wrong_operation'):
                    for record in records:
                        record['operation'] = 'other_operation'
                if config.get('bundle_duplicate') and records:
                    records.append(records[-1])
                if config.get('bundle_missing'):
                    records = []
                bundle = pathlib.Path(arguments['path'])
                bundle.write_text(json.dumps({'records': records}) + '\n')
                value = None
                content = {'result': {'path': str(bundle), 'bytes': bundle.stat().st_size,
                                      'records': len(records), 'unknowns': 0}}
            if name != 'export_evidence_bundle':
                content['normalized_result'] = value
                retained.append(content)
        result = {'structuredContent': content}
        if ((name == 'list_procedures' and config.get('oversized_inventory')) or
                (name == 'analyze_function' and config.get('oversized_dossier'))):
            ident = content['evidence_id']
            error = {'error': {'code': 'resource_constraint', 'details': {
                'operation': name, 'resource': config.get('constraint_resource', 'transport'),
                'reported_limits': {'evidence_id': ident,
                    'evidence_reference': {'kind': 'retained-evidence', 'evidence_id': ident}}}}}
            result = {'isError': True, 'content': [{'type': 'text', 'text': json.dumps(error)}]}
        if name == 'close_binary' and config.get('fail_close'):
            result = {'isError': True, 'content': [{'type': 'text', 'text': 'cleanup_incomplete'}]}
    print(json.dumps({'jsonrpc': '2.0', 'id': request['id'], 'result': result}), flush=True)
'''


class ReaDraftTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / "repo"
        (self.root / "symbols").mkdir(parents=True)
        (self.root / "symbols/ghidra_functions.csv").write_text(
            "address,size\n0x00401000,1\n0x00401010,1\n0x00401020,1\n")
        (self.root / "symbols/functions.csv").write_text("address,name\n")
        self.image = self.base / "input.elf"
        self.image.write_bytes(build([(0x1000, bytes([0xc3]) * 64, 64, 5)], [], []))
        image_hash = drafts.digest(self.image)
        (self.root / "target.sha256").write_text(image_hash + "  eboot.elf\n")
        (self.root / "target.image.sha256").write_text(drafts.elf_layout(self.image)[1] + "  eboot.elf\n")
        self.config = self.base / "peer.json"
        self.configuration = {"log": str(self.base / "requests.jsonl"), "image_sha256": image_hash}
        self.peer = self.base / "peer.py"
        self.peer.write_text(PEER)
        self.args = argparse.Namespace(root=self.root, image=self.image, provenance=None,
            output=self.base / "private", addresses=[], all=True, image_base=0x400000,
            provider="hopper", analysis_base=0, limit=8, timeout=3, resume=False, retry_failed=False, rea=None)

    def tearDown(self):
        self.temp.cleanup()

    def run_export(self):
        self.config.write_text(json.dumps(self.configuration))
        return drafts.run(self.args, command=[sys.executable, str(self.peer), str(self.config)])

    def requests(self):
        path = Path(self.configuration["log"])
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def calls(self, name):
        return [request for request in self.requests()
                if request.get("method") == "tools/call" and request["params"]["name"] == name]

    def state(self):
        return json.loads((self.args.output / "drafts.json").read_text())

    def test_exports_exact_entries_and_keeps_absence_separate_from_pseudocode(self):
        before = self.image.read_bytes()
        result = self.run_export()
        self.assertEqual(result["statuses"], {"dossier": 2, "missing-procedure": 1})
        self.assertEqual(result["pseudocode"], 2)
        self.assertEqual(result["cleanup"], "closed")
        self.assertEqual(len(self.calls("open_binary")), 1)
        self.assertEqual(len(self.calls("analyze_function")), 2)
        self.assertEqual(len(self.calls("export_evidence_bundle")), 1)
        self.assertEqual(len(self.calls("close_binary")), 1)
        self.assertEqual(self.image.read_bytes(), before)
        self.assertEqual(self.state()["analysis_profile_digest"], "p" * 64)

    def test_neighbor_entry_is_rejected_before_analysis(self):
        self.configuration["neighbor"] = True
        self.run_export()
        self.assertEqual(self.calls("analyze_function"), [])
        row = self.state()["functions"]["0x00401000"]
        self.assertEqual(row["status"], "error")
        self.assertIn("neighboring", row["error"])

    def test_file_mapping_disagreement_is_rejected_before_analysis(self):
        self.configuration["mapping_shift"] = 1
        self.run_export()
        self.assertEqual(self.calls("analyze_function"), [])
        self.assertIn("mapping disagrees", self.state()["functions"]["0x00401000"]["error"])

    def test_rebased_provider_retains_original_elf_file_coordinates(self):
        self.configuration["analysis_base"] = self.args.analysis_base = 0x400000
        self.assertEqual(self.run_export()["pseudocode"], 2)
        self.assertEqual(self.state()["functions"]["0x00401000"]["analysis_address"], "0x401000")

    def test_resume_skips_hash_valid_dossiers_and_prior_absence(self):
        self.run_export()
        self.args.resume = True
        result = self.run_export()
        self.assertEqual(result["attempted"], 0)
        self.assertEqual(len(self.calls("analyze_function")), 2)

    def test_missing_first_entry_cannot_starve_later_entries(self):
        self.args.addresses = ["0x00401020", "0x00401000"]
        self.args.limit = 1
        self.assertEqual(self.run_export()["statuses"], {"missing-procedure": 1})
        self.args.resume = True
        self.assertEqual(self.run_export()["statuses"], {"dossier": 1})
        self.args.retry_failed = True
        self.assertEqual(self.run_export()["statuses"], {"missing-procedure": 1})
        self.assertEqual(len(self.calls("analyze_function")), 1)

    def test_resume_refuses_changed_evidence(self):
        self.run_export()
        row = self.state()["functions"]["0x00401000"]
        (self.args.output / row["file"]).write_text("changed")
        self.args.resume = True
        with self.assertRaisesRegex(ValueError, "resume evidence changed"):
            self.run_export()

    def test_resume_refuses_native_profile_change(self):
        self.args.limit = 1
        self.run_export()
        self.args.resume = True
        self.configuration["profile"] = "q" * 64
        with self.assertRaisesRegex(RuntimeError, "native analysis profile changed"):
            self.run_export()
        self.assertEqual(len(self.calls("close_binary")), 2)

    def test_limit_bounds_uncached_work(self):
        self.args.limit = 1
        self.assertEqual(self.run_export()["attempted"], 1)
        self.args.resume = True
        self.assertEqual(self.run_export()["attempted"], 1)
        self.assertEqual(len(self.calls("analyze_function")), 2)

    def test_empty_pseudocode_does_not_count_as_decompilation(self):
        self.configuration["no_pseudocode"] = True
        self.assertEqual(self.run_export()["pseudocode"], 0)

    def test_cleanup_failure_is_reported_and_retained(self):
        self.configuration["fail_close"] = True
        with self.assertRaisesRegex(RuntimeError, "close_binary"):
            self.run_export()
        self.assertEqual(self.state()["runs"][-1]["cleanup"], "unconfirmed")

    def test_dossier_identity_is_checked_again_after_metadata(self):
        self.configuration["wrong_dossier"] = True
        self.run_export()
        self.assertEqual(self.state()["functions"]["0x00401000"]["status"], "error")
        self.assertIn("different procedure", self.state()["functions"]["0x00401000"]["error"])

    def test_timeout_records_unconfirmed_cleanup_without_issuing_concurrent_calls(self):
        self.configuration["stall_open"] = True
        waits = []

        class PendingOpenTimeoutClient(drafts.Client):
            def request(self, method, params):
                self.inject_timeout = method == 'tools/call' and params.get('name') == 'open_binary'
                return super().request(method, params)

            def _receive(self, timeout):
                if self.inject_timeout:
                    waits.append(timeout)
                    raise drafts.queue.Empty
                return super()._receive(timeout)

        # Initialize and discover tools normally. Inject expiration only after the native open
        # has been sent, both for its initial wait and the cancellation drain; no wall clock race.
        with mock.patch.object(drafts, 'Client', PendingOpenTimeoutClient):
            with self.assertRaisesRegex(RuntimeError, "timed out; pending=True"):
                self.run_export()
        self.assertEqual(self.state()["runs"][-1]["cleanup"], "unconfirmed")
        self.assertEqual(len(waits), 2)
        opened = self.calls("open_binary")
        self.assertEqual(len(opened), 1)
        cancellations = [request for request in self.requests()
                         if request.get('method') == 'notifications/cancelled']
        self.assertEqual(len(cancellations), 1)
        self.assertEqual(cancellations[0]['params']['requestId'], opened[0]['id'])
        self.assertEqual([request['method'] for request in self.requests()[:3]],
                         ['initialize', 'notifications/initialized', 'tools/list'])
        self.assertEqual(self.calls("analyze_function"), [])
        self.assertEqual(self.calls("close_binary"), [])

    def test_resume_refuses_loader_environment_change(self):
        self.args.limit = 1
        self.run_export()
        self.args.resume = True
        key = "HOPPER_LOADER_ARGS_JSON"
        before = os.environ.get(key)
        try:
            os.environ[key] = '["-A"]'
            with self.assertRaisesRegex(ValueError, "resume identity changed"):
                self.run_export()
        finally:
            if before is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = before

    def test_refuses_repository_output_and_unknown_addresses_before_startup(self):
        self.args.output = self.root / "drafts"
        with self.assertRaisesRegex(ValueError, "outside the repository"):
            self.run_export()
        self.assertEqual(self.requests(), [])
        self.args.output = self.base / "private"
        self.args.addresses = ["0x401030"]
        with self.assertRaisesRegex(ValueError, "absent from"):
            self.run_export()

    def test_changed_loaded_image_is_rejected_before_startup(self):
        changed = bytearray(self.image.read_bytes())
        changed[0x1000] ^= 1
        self.image.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "pinned loaded image"):
            self.run_export()
        self.assertEqual(self.requests(), [])

    def test_derived_image_requires_matching_provenance(self):
        self.image.write_bytes(self.image.read_bytes() + b"synthetic derived metadata")
        with self.assertRaisesRegex(ValueError, "requires --provenance"):
            self.run_export()
        loaded_hash = drafts.elf_layout(self.image)[1]
        provenance = {"schema_version": 1, "kind": "paleblood_rea_analysis_image",
            "original": {"sha256": drafts.pin(self.root, "target.sha256"), "loaded_image_sha256": loaded_hash},
            "derived": {"sha256": drafts.digest(self.image), "loaded_image_sha256": loaded_hash},
            "catalog": {"sha256": drafts.digest(self.root / "symbols/ghidra_functions.csv")},
            "names": {"sha256": drafts.digest(self.root / "symbols/functions.csv")},
            "address_transform": {"subtract": 0x400000}, "preservation": {"load_segments_equal": True}}
        self.args.provenance = self.base / "provenance.json"
        self.args.provenance.write_text(json.dumps(provenance))
        self.configuration["image_sha256"] = drafts.digest(self.image)
        self.assertEqual(self.run_export()["pseudocode"], 2)

    def test_oversized_inventory_recovers_exact_retained_record_without_repeating_producer(self):
        self.configuration.update(oversized_inventory=True, bundle_neighbor=True)
        report = self.run_export()
        self.assertEqual(report["statuses"], {"dossier": 2, "missing-procedure": 1})
        self.assertEqual(len(self.calls("list_procedures")), 1)
        self.assertEqual(len(self.calls("analyze_function")), 2)
        self.assertEqual(len(self.calls("export_evidence_bundle")), 2)  # recovery, then final ledger
        run = self.args.output / report["directory"]
        self.assertTrue(json.loads((run / "procedures.json").read_text())["result"]["isError"])
        recovered = json.loads((run / "procedures-retained-evidence.json").read_text())
        self.assertEqual(recovered["operation"], "list_procedures")
        self.assertEqual(recovered["subject"]["digest"]["sha256"], drafts.digest(self.image))

    def test_oversized_dossiers_recover_once_and_resume_uses_the_complete_record_hash(self):
        self.configuration.update(oversized_dossier=True, bundle_neighbor=True)
        report = self.run_export()
        self.assertEqual(report["pseudocode"], 2)
        self.assertEqual(len(self.calls("analyze_function")), 2)
        self.assertEqual(len(self.calls("export_evidence_bundle")), 3)
        row = self.state()["functions"]["0x00401000"]
        self.assertTrue(row["file"].endswith("-retained-evidence.json"))
        self.assertEqual(json.loads((self.args.output / row["file"]).read_text())["operation"], "analyze_function")
        self.args.resume = True
        self.assertEqual(self.run_export()["attempted"], 0)
        self.assertEqual(len(self.calls("analyze_function")), 2)

    def test_retained_inventory_recovery_refuses_wrong_target_operation_missing_or_duplicate_id(self):
        for field in ("bundle_wrong_target", "bundle_wrong_operation", "bundle_missing", "bundle_duplicate"):
            with self.subTest(field=field):
                self.args.output = self.base / field
                self.configuration = {"log": str(self.base / (field + ".jsonl")),
                    "image_sha256": drafts.digest(self.image), "oversized_inventory": True, field: True}
                with self.assertRaisesRegex(RuntimeError, "retained transport recovery failed"):
                    self.run_export()
                self.assertEqual(len(self.calls("list_procedures")), 1)
                self.assertEqual(self.calls("analyze_function"), [])
                self.assertEqual(len(self.calls("close_binary")), 1)

    def test_nontransport_resource_error_does_not_trigger_retained_recovery(self):
        self.configuration.update(oversized_inventory=True, constraint_resource="memory")
        with self.assertRaisesRegex(RuntimeError, "resource_constraint"):
            self.run_export()
        self.assertEqual(len(self.calls("export_evidence_bundle")), 1)  # final ledger only
        self.assertEqual(len(self.calls("list_procedures")), 1)


if __name__ == "__main__":
    unittest.main()
