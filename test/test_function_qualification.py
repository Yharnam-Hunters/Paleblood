# SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic tests for ownership and execution qualification."""
from __future__ import annotations

import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import function_qualification as qualification  # noqa: E402


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.elf = self.root / "fixture.elf"
        self.elf.write_bytes(self.make_elf(bytes.fromhex("31c0c3")))
        self.sha, self.segments = qualification.read_elf_executable_segments(self.elf)

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def make_elf(code: bytes) -> bytes:
        header = bytearray(64)
        header[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
        struct.pack_into("<H", header, 16, 0xFE10)
        struct.pack_into("<H", header, 18, 0x3E)
        struct.pack_into("<I", header, 20, 1)
        struct.pack_into("<Q", header, 32, 64)
        struct.pack_into("<H", header, 52, 64)
        struct.pack_into("<H", header, 54, 56)
        struct.pack_into("<H", header, 56, 1)
        program = struct.pack("<IIQQQQQQ", 1, 5, 120, 0x100, 0, len(code), len(code), 0x1000)
        return bytes(header) + program + bytes(120 - len(header) - len(program)) + code

    @staticmethod
    def snapshot(claims=(), complete=True):
        return {"snapshot_complete": complete, "claims": list(claims),
                "checked_at": int(time.time()),
                "sources": [{"source": source, "status": "complete", "checked_at": int(time.time())}
                            for source in sorted(qualification.REQUIRED_OWNERSHIP_SOURCES)]}

    @staticmethod
    def row(address="0x00400100", code_size=3, instruction_count=2):
        return {"address": address, "size_bytes": code_size, "instruction_count": instruction_count,
                "name": None, "replacement_status": "original", "cache_key": "catalog-row",
                "calls": {"analysis_complete": True, "direct": [], "indirect": [], "imports": [],
                          "unresolved_direct": [], "unresolved_indirect": []},
                "memory_dependencies": {"static_references": None, "complete": False}}

    def test_contributor_and_automation_conflict_is_not_unclaimed(self):
        owner = qualification.classify_ownership("0x00400100", "candidate", self.snapshot([
            {"address": "0x00400100", "source": "issue", "kind": "contributor"},
            {"address": "0x00400100", "source": "worktree", "kind": "local_assignment"}]))
        self.assertEqual(owner["status"], "ownership_conflict")
        self.assertTrue(owner["blockers"])

    def test_missing_ownership_source_remains_unknown(self):
        owner = qualification.classify_ownership("0x00400100", "candidate", self.snapshot(complete=False))
        self.assertEqual(owner["status"], "unknown")
        self.assertTrue(owner["blockers"])

    def test_ownership_snapshot_must_be_complete_timestamped_and_fresh(self):
        current = int(time.time())
        fresh = self.snapshot()
        stale = {**fresh, "checked_at": current - qualification.OWNERSHIP_MAX_AGE_SECONDS - 1}
        missing_timestamp = {"snapshot_complete": True, "sources": [{"status": "complete"}]}
        self.assertTrue(qualification.ownership_snapshot_is_current(fresh, now=current))
        self.assertFalse(qualification.ownership_snapshot_is_current(stale, now=current))
        self.assertFalse(qualification.ownership_snapshot_is_current(missing_timestamp, now=current))

    def test_subsystem_claim_covers_functions_with_unresolved_names(self):
        owner = qualification.classify_ownership("0x00400100", None,
                                                self.snapshot([{"system": "kernel", "kind": "contributor",
                                                                "source": "issue #34"}]), "kernel")
        self.assertEqual(owner["status"], "contributor_owned")

    def test_active_worker_and_implemented_registry_statuses(self):
        worker = qualification.classify_ownership("0x00400100", None, self.snapshot([
            {"address": "0x00400100", "kind": "local_assignment"}]))
        implemented = qualification.classify_ownership("0x00400100", None, self.snapshot([
            {"address": "0x00400100", "kind": "implemented"}]))
        self.assertEqual(worker["status"], "active_automation_worker")
        self.assertEqual(implemented["status"], "already_implemented")

    def test_cross_tool_leaf_is_fixture_and_original_execution_eligible(self):
        row = self.row()
        result = qualification.boundary_and_dependency_evidence(
            row, [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"}],
            self.elf, self.segments)
        self.assertEqual(result["boundary_class"], "cross_tool_supported")
        self.assertTrue(result["eligible_for_fixture_generation"])
        self.assertTrue(result["eligible_for_original_execution"])
        self.assertFalse(result["eligible_for_replacement_development"])

    def test_indirect_call_blocks_fixture_generation(self):
        self.elf.write_bytes(self.make_elf(bytes.fromhex("ffd0c3")))
        _, self.segments = qualification.read_elf_executable_segments(self.elf)
        row = self.row(code_size=3, instruction_count=2)
        row["calls"]["indirect"].append({"target": None})
        result = qualification.boundary_and_dependency_evidence(
            row, [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"}],
            self.elf, self.segments)
        self.assertFalse(result["eligible_for_fixture_generation"])
        self.assertIn("indirect_calls", result["dependencies"])

    def test_unknown_argument_memory_dependency_blocks_fixture(self):
        self.elf.write_bytes(self.make_elf(bytes.fromhex("488b07c3")))
        _, self.segments = qualification.read_elf_executable_segments(self.elf)
        row = self.row(code_size=4, instruction_count=2)
        result = qualification.boundary_and_dependency_evidence(
            row, [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"}],
            self.elf, self.segments)
        self.assertFalse(result["eligible_for_fixture_generation"])
        self.assertIn("data-memory", " ".join(result["blockers"]))

    def test_lea_calculates_an_address_without_reading_memory(self):
        for code in (bytes.fromhex("488d4708c3"), bytes.fromhex("488d442408c3")):
            self.elf.write_bytes(self.make_elf(code))
            _, segments = qualification.read_elf_executable_segments(self.elf)
            row = self.row(code_size=len(code), instruction_count=2)
            result = qualification.boundary_and_dependency_evidence(
                row, [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"}], self.elf, segments)
            decoded = qualification._decode_with_objdump(code, 0x400100)
            self.assertTrue(decoded[0]["is_address_calculation"])
            self.assertFalse(decoded[0]["is_memory"])
            self.assertFalse(decoded[0]["is_stack_memory"])
            self.assertTrue(result["eligible_for_original_execution"])
            self.assertFalse(result["eligible_for_replacement_development"])
            self.assertEqual(result["dependencies"]["memory_accesses"][0]["access"], "address_calculation")

    def test_load_through_a_lea_result_still_requires_memory_fixtures(self):
        code = bytes.fromhex("488d4708488b00c3")
        self.elf.write_bytes(self.make_elf(code))
        _, segments = qualification.read_elf_executable_segments(self.elf)
        row = self.row(code_size=len(code), instruction_count=3)
        result = qualification.boundary_and_dependency_evidence(
            row, [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"}], self.elf, segments)
        self.assertFalse(result["eligible_for_original_execution"])
        self.assertEqual([access["access"] for access in result["dependencies"]["memory_accesses"]],
                         ["address_calculation", "read", "read"])

    def test_boundary_uncertainty_and_overlaps_are_blocked(self):
        row = self.row()
        uncertain = qualification.boundary_and_dependency_evidence(row, [], self.elf, self.segments)
        overlap = qualification.boundary_and_dependency_evidence(
            row, [], self.elf, self.segments, {row["address"]})
        self.assertEqual(uncertain["boundary_class"], "ghidra_only")
        self.assertEqual(overlap["boundary_class"], "overlapping_extent")
        self.assertFalse(overlap.get("eligible_for_original_execution", False))

    def test_missing_function_extent_stays_unknown(self):
        row = self.row()
        row["size_bytes"] = None
        result = qualification.boundary_and_dependency_evidence(row, [], self.elf, self.segments)
        self.assertEqual(result["boundary_class"], "ghidra_only")
        self.assertIn("function extent is missing", result["blockers"])

    def test_import_thunk_is_classified_and_excluded(self):
        self.elf.write_bytes(self.make_elf(bytes.fromhex("ff2500000000")))
        _, self.segments = qualification.read_elf_executable_segments(self.elf)
        row = self.row(code_size=6, instruction_count=1)
        result = qualification.boundary_and_dependency_evidence(
            row, [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"}],
            self.elf, self.segments)
        self.assertEqual(result["function_kind"], "import_thunk_or_tail_stub")
        self.assertEqual(result["boundary_class"], "thunk_or_stub")

    def test_cache_key_invalidates_for_catalog_ownership_and_tool_changes(self):
        row = self.row()
        owner_a = qualification.classify_ownership(row["address"], None, self.snapshot())
        owner_b = qualification.classify_ownership(row["address"], None,
                                                   self.snapshot([{"address": row["address"], "kind": "contributor"}]))
        base = qualification.qualification_key(row, {"catalog_identity": "a"}, owner_a, self.sha)
        changed_function = {**row, "cache_key": "changed-function-metadata"}
        self.assertNotEqual(base, qualification.qualification_key(
            changed_function, {"catalog_identity": "a"}, owner_a, self.sha))
        self.assertNotEqual(base, qualification.qualification_key(row, {"catalog_identity": "b"}, owner_a, self.sha))
        self.assertNotEqual(base, qualification.qualification_key(row, {"catalog_identity": "a"}, owner_b, self.sha))
        self.assertNotEqual(base, qualification.qualification_key(row, {"catalog_identity": "a"}, owner_a, "b" * 64))
        self.assertNotEqual(base, qualification.qualification_key(
            row, {"catalog_identity": "a", "qualification_tools": {"objdump_version": "2"}}, owner_a, self.sha))
        with patch.object(qualification, "QUALIFICATION_VERSION", "new-tool-version"):
            self.assertNotEqual(base, qualification.qualification_key(row, {"catalog_identity": "a"}, owner_a, self.sha))

    def test_blocked_ownership_never_promotes_qualification(self):
        row = self.row()
        row["replacement_status"] = "untracked"
        result = qualification.qualify_rows([row], [], self.snapshot(complete=False), self.sha,
                                            self.elf, self.segments, {"catalog_identity": "fixture"}, 1)[0]
        self.assertEqual(result["classification"], "conditionally_eligible")
        self.assertFalse(result["eligible_for_fixture_generation"])
        self.assertFalse(result["eligible_for_original_execution"])
        self.assertFalse(result["eligible_for_replacement_development"])

    def test_interrupted_qualification_transaction_preserves_previous_campaign(self):
        state = self.root / "qualification.sqlite3"
        database = qualification.catalog.connect(state)
        database.execute("INSERT INTO metadata VALUES ('catalog_identity', ?)",
                         (qualification.catalog.canonical("fixture-catalog"),))
        database.commit()
        database.close()
        owner_snapshot = {"snapshot_sha256": "fixture-owner", "claims": [], "sources": [],
                          "snapshot_complete": True}
        qualification.cache_snapshot(state, owner_snapshot)
        previous = {"address": "0x00400100", "qualification_cache_key": "old", "classification": "blocked",
                    "qualification_version": qualification.QUALIFICATION_VERSION,
                    "catalog_identity": "fixture-catalog", "ownership_snapshot_sha256": "fixture-owner"}
        changed = {**previous, "qualification_cache_key": "new", "classification": "eligible"}
        qualification.save_qualifications(state, [previous])
        with self.assertRaisesRegex(RuntimeError, "interrupted"):
            qualification.save_qualifications(state, [changed], fail_after=1)
        self.assertEqual(qualification.load_qualifications(state), [previous])

    def test_tool_change_hides_stale_eligible_cache_until_requalified(self):
        state = self.root / "tools.sqlite3"
        database = qualification.catalog.connect(state)
        database.execute("INSERT INTO metadata VALUES ('catalog_identity', ?)",
                         (qualification.catalog.canonical("fixture-catalog"),))
        database.commit()
        database.close()
        checked_at = int(time.time())
        sources = self.snapshot()['sources']
        qualification.cache_snapshot(state, {"snapshot_sha256": "owner", "claims": [],
                                             "checked_at": checked_at, "snapshot_complete": True,
                                             "sources": sources})
        cached = {"address": "0x00400100", "qualification_cache_key": "key",
                  "qualification_version": qualification.QUALIFICATION_VERSION,
                  "catalog_identity": "fixture-catalog", "ownership_snapshot_sha256": "owner",
                  "qualification_tools": {"objdump_version": "1"},
                  "ownership": {"evidence": [{"checked_at": checked_at - 1}]},
                  "eligible_for_original_execution": True}
        qualification.save_qualifications(state, [cached])
        current = qualification.load_qualifications(state, {"objdump_version": "1"})
        self.assertEqual(current[0]["ownership"]["evidence"], sources)
        self.assertEqual(current[0]["ownership_revalidated_at"], checked_at)
        self.assertEqual(qualification.load_qualifications(state, {"objdump_version": "2"}), [])

    def test_campaign_order_is_deterministic(self):
        rows = [self.row("0x00400100"), self.row("0x00400200")]
        for row in rows:
            row["replacement_status"] = "untracked"
        edges = [{"caller": "0x004000f0", "callee": row["address"], "kind": "direct"} for row in rows]
        first = qualification.qualify_rows(rows, edges, self.snapshot(), self.sha, self.elf, self.segments,
                                           {"catalog_identity": "fixture"}, 2)
        second = qualification.qualify_rows(rows, edges, self.snapshot(), self.sha, self.elf, self.segments,
                                            {"catalog_identity": "fixture"}, 2)
        self.assertEqual([item["address"] for item in first], [item["address"] for item in second])
        cached = qualification.qualify_rows(rows, edges, self.snapshot(), self.sha, self.elf, self.segments,
                                            {"catalog_identity": "fixture"}, 2,
                                            {item["address"]: item for item in first})
        self.assertEqual(cached, first)

    def test_repeatable_decoder_and_claim_extraction(self):
        first = qualification._decode_with_objdump(bytes.fromhex("31c0c3"), 0x1000)
        second = qualification._decode_with_objdump(bytes.fromhex("31c0c3"), 0x1000)
        self.assertEqual(first, second)
        claims = qualification._extract_claims("I claim 0x00400100 and candidate", "issue", "user", "https://github.com/x/y/issues/1", {"candidate"})
        self.assertEqual(claims[0]["address"], "0x00400100")
        self.assertEqual(claims[1]["symbol"], "candidate")
        filtered = qualification._extract_claims("NID 0xdeadbeef, function 0x00400100", "issue", "user",
                                                 "https://github.com/x/y/issues/1", set(), True,
                                                 {"0x00400100"})
        self.assertEqual([claim["address"] for claim in filtered], ["0x00400100"])

    def test_rechecking_unchanged_ownership_does_not_invalidate_cache(self):
        row = self.row()
        snapshot_a = {**self.snapshot(), "snapshot_sha256": "stable",
                      "sources": [{"source": "issues", "status": "complete", "checked_at": 1}]}
        snapshot_b = {**self.snapshot(), "snapshot_sha256": "stable",
                      "sources": [{"source": "issues", "status": "complete", "checked_at": 2}]}
        owner_a = qualification.classify_ownership(row["address"], None, snapshot_a)
        owner_b = qualification.classify_ownership(row["address"], None, snapshot_b)
        self.assertEqual(qualification.qualification_key(row, {"catalog_identity": "a"}, owner_a, self.sha),
                         qualification.qualification_key(row, {"catalog_identity": "a"}, owner_b, self.sha))


if __name__ == "__main__":
    unittest.main()
