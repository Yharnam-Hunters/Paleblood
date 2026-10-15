# SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic contract tests for the differential workflow and evidence store."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import differential
import verify


ABI = {
    "calling_convention": "sysv_amd64", "confidence": "supported",
    "evidence": ["test fixture contract"], "stack_alignment": 16,
    "arguments": [{"register": "rdi", "type": "uint32_t", "direction": "input"}],
    "returns": {"type": "uint32_t"}, "input_regions": [], "writable_regions": [],
    "execution_limits": {"timeout_seconds": 5},
}


class Differential(unittest.TestCase):
    def case(self):
        return {"schema": 1, "address": "0x00400000", "id": "case", "abi": ABI,
                "args": {"rdi": "0x2"}, "buffers": {}, "memory": [],
                "returns": "i32", "imports": [], "stubs": []}

    def test_abi_unknown_and_incomplete_fail_closed(self):
        self.assertTrue(differential.validate_abi(None))
        incomplete = dict(ABI)
        del incomplete["stack_alignment"]
        self.assertTrue(differential.validate_abi(incomplete))
        self.assertEqual(differential.validate_abi(ABI), [])
        bad_limits = {**ABI, "execution_limits": {"timeout_seconds": 0}}
        self.assertTrue(any("timeout_seconds" in reason for reason in differential.validate_abi(bad_limits)))
        self.assertEqual(differential.effective_timeout(None, ABI), 5)
        self.assertEqual(differential.effective_timeout(4, ABI), 4)
        with self.assertRaisesRegex(differential.DifferentialError, "fixture limit"):
            differential.effective_timeout(6, ABI)

    def test_fixture_loading_checks_address_and_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "case.json"
            path.write_text(json.dumps(self.case()))
            rows, abi = differential.load_fixtures(Path(tmp), "0x400000")
            self.assertEqual(len(rows), 1)
            self.assertEqual(abi, ABI)
            self.assertEqual(differential.digest_file(path), differential.digest_file(path))
            with self.assertRaisesRegex(differential.DifferentialError, "address"):
                differential.load_fixtures(Path(tmp), "0x400100")
            path.write_text(json.dumps({**self.case(), "args": {}}))
            with self.assertRaisesRegex(differential.DifferentialError, "omits declared ABI arguments"):
                differential.load_fixtures(Path(tmp), "0x400000")

    def test_exploration_is_seeded_bounded_and_abi_guided(self):
        case = self.case()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "case.json"
            path.write_text(json.dumps(case))
            rows = [(path, case)]
            first = differential.explore_fixtures(rows, ABI, 5, 77)
            second = differential.explore_fixtures(rows, ABI, 5, 77)
            self.assertEqual(first, second)
            self.assertEqual(len(first), 4)
            values = {item["args"]["rdi"] for item in first}
            self.assertTrue(values <= {"0x0", "0x1", "0xffffffff", "0x2"})

    def test_exploration_generates_opted_in_alias_and_float_edges(self):
        abi = {**ABI, "arguments": [
            {"register": "rdi", "type": "uint8_t *", "direction": "inout", "alias_safe": True},
            {"register": "rsi", "type": "uint8_t *", "direction": "inout", "alias_safe": True},
            {"register": "xmm0", "type": "float", "direction": "input"},
        ]}
        case = self.case()
        case["args"] = {"rdi": "buf:left", "rsi": "buf:right", "xmm0": "00000000000000000000000000000000"}
        case["buffers"] = {"left": {"size": 4}, "right": {"size": 4}}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "base.json"
            path.write_text(json.dumps(case))
            generated = differential.explore_fixtures([(path, case)], abi, 100, 19)
        alias = next(row for row in generated if "alias" in row.get("exploration", {}))
        self.assertEqual(alias["args"]["rsi"], "buf:left")
        float_values = {row["args"]["xmm0"] for row in generated if row.get("exploration", {}).get("register") == "xmm0"}
        self.assertGreaterEqual(len(float_values), 8)
        self.assertIn("00000080000000000000000000000000", float_values)  # negative zero

    def test_minimizer_preserves_mismatch_class_and_is_bounded(self):
        case = self.case()
        case["memory"] = [{"addr": "0x00401000", "bytes": "01020304"}]
        case["args"]["rdx"] = "0x5"

        def mismatch(candidate):
            return ["ret.rax: differs"] if candidate["args"]["rdi"] != "0x0" else []

        reduced, attempts = differential.minimize_fixture(case, mismatch, 3)
        self.assertGreater(attempts, 0)
        self.assertLessEqual(attempts, 3)
        self.assertEqual(mismatch(reduced), ["ret.rax: differs"])
        self.assertEqual(case["memory"][0]["bytes"], "01020304")

    def test_instruction_trace_diagnostics_separate_path_changes_from_state_mismatches(self):
        event = {"access": "read", "region": "buf:value", "region_offset": "+0x0", "width": 4,
                 "before": "01020304", "after": None, "valid_region": True,
                 "state_before": {"rax": "0x0"}}
        original = {"schema": 1, "address": "0x400000", "cases": [{"id": "x", "ret": {"rax": "0x1"},
                    "writes": [], "instruction_trace": {"complete": True, "events": [event]}}]}
        replacement = {"schema": 1, "address": "0x400000", "cases": [{"id": "x", "ret": {"rax": "0x1"},
                       "writes": [], "instruction_trace": {"complete": True, "events": [event, dict(event)]}}]}
        result = verify.compare(original, replacement)
        self.assertEqual(result["failed"], 0)
        diagnostic = result["instruction_diagnostics"][0]["first_divergence"]
        self.assertEqual(diagnostic["classification"], "bounded_memory_trace_difference")
        self.assertFalse(diagnostic["observable_result_mismatch"])
        replacement["cases"][0]["instruction_trace"]["complete"] = False
        self.assertEqual(verify.compare(original, replacement)["failed"], 1)
        replacement["cases"][0].pop("instruction_trace")
        self.assertEqual(verify.compare(original, replacement)["failed"], 1)

    def test_instruction_trace_bounds_violations_fail_closed(self):
        event = {"access": "write", "region": "unmapped_or_undeclared", "region_offset": "0x900000000",
                 "width": 1, "valid_region": False, "before": "00", "after": "00"}
        result = {"schema": 1, "address": "0x400000", "cases": [{"id": "oob", "writes": [],
                  "instruction_trace": {"complete": False, "events": [event], "stop_reason": "bounds"}}]}
        compared = verify.compare(result, result)
        self.assertEqual(compared["failed"], 1)
        self.assertTrue(any("bounds violation" in diff for diff in compared["failures"][0]["diffs"]))

    def test_catalog_cache_tracks_executable_abi_source_and_fixture(self):
        base = differential.run_identity("0x400000", "exe", "fixture", ABI, None)
        for changed in (
            differential.run_identity("0x400000", "other-exe", "fixture", ABI, None),
            differential.run_identity("0x400000", "exe", "other-fixture", ABI, None),
            differential.run_identity("0x400000", "exe", "fixture", {**ABI, "stack_alignment": 32}, None),
            differential.run_identity("0x400000", "exe", "fixture", ABI, None,
                                       build_config={"build_type": "Debug"}),
        ):
            self.assertNotEqual(differential.digest_bytes(differential.canonical(base).encode()),
                                differential.digest_bytes(differential.canonical(changed).encode()))

    def test_build_identity_is_path_free_and_tracks_flags(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "build"
            game = build / "game"
            game.mkdir(parents=True)
            library = game / "libbbgame.so"
            library.write_bytes(b"library")
            (build / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Debug\n"
                "CMAKE_C_COMPILER:FILEPATH=/opt/toolchain/bin/clang\n"
                "CMAKE_CXX_COMPILER:FILEPATH=/opt/toolchain/bin/clang++\n"
                "CMAKE_CXX_FLAGS:STRING=-O0 -g\n", encoding="utf-8")
            identity = differential.build_configuration(library)
            self.assertEqual(identity["build_type"], "Debug")
            self.assertEqual(identity["compiler_commands"]["CMAKE_CXX_COMPILER"], "clang++")
            self.assertNotIn(tmp, json.dumps(identity))
            compilers = differential.compiler_identity(identity)
            self.assertIn("clang", compilers["clang"])
            self.assertIn("clang", compilers["clang++"])

    def test_resume_store_uses_phase1_database_and_atomic_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = Path(tmp) / "catalog.sqlite3"
            differential.save_case(state, "key", "original", "case-a", {"ret": {"rax": "0x2"}})
            self.assertEqual(differential.cached_case(state, "key", "original", "case-a"),
                             {"ret": {"rax": "0x2"}})
            self.assertIsNone(differential.cached_case(state, "other-key", "original", "case-a"))

    def test_verification_stages_are_recorded_separately(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = Path(tmp) / "catalog.sqlite3"
            differential.update_verification_state(state, "0x400000", "fixture-key",
                                                   fixture_status="validated",
                                                   blockers=["ABI missing"])
            db = differential.database(state)
            try:
                row = db.execute("SELECT fixture_status,original_status,differential_status,blockers "
                                 "FROM verification_state").fetchone()
            finally:
                db.close()
            self.assertEqual(row[:3], ("validated", "not_run", "not_run"))
            self.assertEqual(json.loads(row[3]), ["ABI missing"])

    def test_atomic_write_replaces_only_complete_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "fixture.json"
            differential.atomic_write(target, b'{"complete":true}\n')
            self.assertEqual(target.read_bytes(), b'{"complete":true}\n')
            self.assertEqual(list(Path(tmp).glob("*.tmp")), [])

    def test_comparator_rejects_required_and_opted_in_register_mismatches(self):
        original = {"id": "x", "ret": {"rax": "0x1"}, "preserved": {"rbx": "kept"},
                    "registers": {"rdi": "0x1"}, "observed_registers": ["rdi"], "writes": [], "calls": []}
        for replacement in (
            {**original, "ret": {"rax": "0x2"}},
            {**original, "preserved": {"rbx": "0x2"}},
            {**original, "registers": {"rdi": "0x2"}},
            {**original, "writes": [{"addr": "buf:out", "bytes": "01"}]},
        ):
            result = verify.compare({"schema": 1, "address": "0x1000", "cases": [original]},
                                    {"schema": 1, "address": "0x1000", "cases": [replacement]})
            self.assertEqual(result["failed"], 1)

    def test_comparator_reports_first_mediated_memory_access_difference(self):
        original = {"schema": 1, "address": "0x1000", "cases": [{"id": "x", "ret": {},
                    "preserved": {}, "writes": [], "calls": [],
                    "access_trace": [{"access": "read", "address": "buf:a", "size": 4}]}]}
        replacement = json.loads(json.dumps(original))
        replacement["cases"][0]["access_trace"][0]["access"] = "write"
        result = verify.compare(original, replacement)
        self.assertEqual(result["failed"], 1)
        self.assertIn("first difference at event 0", result["failures"][0]["diffs"][-1])

    def test_evidence_bundle_is_portable_and_outside_repository(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "bundle"
            stored = {"identity": {"address": "0x00400000", "executable_sha256": "a" * 64,
                                   "fixture_sha256": "b" * 64},
                      "result": {"comparison": {"passed": 1, "failed": 0}}}
            differential.write_bundle(output, stored, "0123456789abcdef")
            raw = (output / "0123456789abcdef" / "evidence.json").read_text()
            self.assertNotIn(str(ROOT), raw)
            self.assertIn("memory_reads", raw)
            original_only = {"identity": stored["identity"],
                             "result": {"kind": "original_only", "execution": {"one": {"ret": {"rax": "0x0"}}}}}
            rendered = differential.bundle_markdown({"identity": stored["identity"],
                                                     "result": original_only["result"], "limitations": [],
                                                     "run_id": "0123456789abcdef"})
            self.assertIn("no replacement comparison", rendered)
            with self.assertRaisesRegex(differential.DifferentialError, "outside the public repository"):
                differential.ensure_private_output(ROOT / "evidence")
            with self.assertRaisesRegex(differential.DifferentialError, "run ID"):
                differential.write_bundle(output, stored, "../../escape")


if __name__ == "__main__":
    unittest.main()
