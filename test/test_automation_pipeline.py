# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic orchestration tests; these are not original-game equivalence evidence."""
import tempfile
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import automation_pipeline as pipeline


def synthetic_result(count=2, complete=True):
    cases = []
    for index in range(count):
        cases.append({"id": f"case-{index}", "instruction_trace": {
            "complete": complete, "events": [{"valid_region": True, "width": 1, "access": "read"}],
            "control_flow_coverage": {"status": "complete" if complete else "partial",
                                       "feedback_eligible": complete, "blocks": ["block"], "edges": []}}})
    return {"comparison": {"cases": count, "passed": count if complete else count - 1,
                            "failed": 0 if complete else 1},
            "original": {"cases": cases}, "replacement": {"cases": cases}}


class AutomationPipeline(unittest.TestCase):
    def test_synthetic_end_to_end_success_is_resumable_and_non_promoting(self):
        identity = {"address": "0x400000", "patch": "sha"}
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "run.json"
            results = {"eligibility": {"qualified": True}, "worktree": {"isolated": True},
                       "fixture_generation": {"recorded": 2, "generated": 1},
                       "build": {"gcc": "ok", "clang": "ok"},
                       "differential": synthetic_result(), "validation": {"passed": 9},
                       "evidence": {"promoted": False}}

            def stage(name):
                if name == "differential":
                    pipeline.assert_result_sufficient(results[name], 2)
                return results[name]

            complete = pipeline.execute_stages(state_path, identity, tuple(results), stage)
            self.assertEqual(complete["status"], "validated")
            self.assertFalse(complete["completed"]["evidence"]["promoted"])
            repeated = pipeline.execute_stages(state_path, identity, tuple(results),
                                               lambda _name: self.fail("completed stage reran"))
            self.assertEqual(repeated["completed"], complete["completed"])

    def test_compilers_compare_fixture_outcomes_not_internal_diagnostic_paths(self):
        gcc = {"cases": 4, "passed": 4, "failed": 0, "instruction_diagnostics": ["gcc path"]}
        clang = {"cases": 4, "passed": 4, "failed": 0, "instruction_diagnostics": ["clang path"]}
        self.assertTrue(pipeline.compiler_outcomes_agree(gcc, clang))
        clang["failed"] = 1
        self.assertFalse(pipeline.compiler_outcomes_agree(gcc, clang))

    def test_candidate_patch_cannot_modify_policy_or_verification_tools(self):
        self.assertEqual(pipeline.patch_scope_blockers(
            ["game/ai/new_function.cpp", "test/test_new_function.cpp", "symbols/functions.csv"]), [])
        self.assertEqual(pipeline.patch_scope_blockers(
            ["tools/differential.py", ".github/workflows/ci.yml", "runtime/boot.c"]),
            ["tools/differential.py", ".github/workflows/ci.yml", "runtime/boot.c"])

    def test_rejects_incomplete_trace_invalid_access_and_insufficient_fixtures(self):
        with self.assertRaisesRegex(pipeline.PipelineError, "insufficient fixture"):
            pipeline.assert_result_sufficient(synthetic_result(1), 2)
        with self.assertRaisesRegex(pipeline.PipelineError, "incomplete instruction trace"):
            pipeline.assert_result_sufficient(synthetic_result(2, complete=False), 2)
        result = synthetic_result()
        result["original"]["cases"][0]["instruction_trace"]["events"][0]["valid_region"] = False
        with self.assertRaisesRegex(pipeline.PipelineError, "invalid memory access"):
            pipeline.assert_result_sufficient(result, 2)
        result = synthetic_result()
        result["original"]["cases"][0]["instruction_trace"]["events"][0].update(
            {"access": "write", "before": "00", "after": "00"})
        pipeline.assert_result_sufficient(result, 2)  # same-value writes remain visible events
        result["original"]["cases"][0]["instruction_trace"]["events"][0].pop("after")
        with self.assertRaisesRegex(pipeline.PipelineError, "write trace lacks before/after"):
            pipeline.assert_result_sufficient(result, 2)

    def test_abi_evidence_must_resolve_to_repository_or_https_reference(self):
        self.assertEqual(pipeline.validate_abi_evidence(["docs/VERIFY.md: fixture ABI"]), [])
        self.assertEqual(pipeline.validate_abi_evidence(["https://example.org/abi"]), [])
        self.assertTrue(pipeline.validate_abi_evidence(["/private/unsupported.json"]))
        self.assertTrue(pipeline.validate_abi_evidence(["missing/evidence.md"]))

    def test_new_candidate_requires_per_fixture_capture_provenance(self):
        valid = [{"id": "run_1", "capture": {"function": "target", "run_id": "run"}}]
        invalid = [{"id": "unattributed"}]
        self.assertEqual(pipeline.recorded_provenance_blockers(
            valid, "target", regression_only=False, verification_records=[], minimum=1), [])
        self.assertTrue(pipeline.recorded_provenance_blockers(
            invalid, "target", regression_only=False, verification_records=[], minimum=1))
        legacy_record = [{"current": True, "cases": "recorded", "passed": 2, "failed": 0}]
        self.assertEqual(pipeline.recorded_provenance_blockers(
            invalid, "target", regression_only=True, verification_records=legacy_record, minimum=2), [])

    def test_interrupted_stage_resumes_from_last_durable_checkpoint(self):
        identity = {"candidate": "0x400000"}
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "run.json"
            completed = []

            def interrupted(name):
                completed.append(name)
                if name == "fixture_generation":
                    raise RuntimeError("simulated interruption")
                return {"ok": True}

            with self.assertRaisesRegex(RuntimeError, "interruption"):
                pipeline.execute_stages(state_path, identity,
                                        ("eligibility", "worktree", "fixture_generation", "differential"),
                                        interrupted)
            resumed = pipeline.execute_stages(
                state_path, identity, ("eligibility", "worktree", "fixture_generation", "differential"),
                lambda name: {"ok": True, "stage": name})
            self.assertEqual(completed, ["eligibility", "worktree", "fixture_generation"])
            self.assertEqual(resumed["last_completed_stage"], "differential")

    def test_draft_pr_creation_is_idempotent(self):
        state = {"status": "validated"}
        saved = []
        calls = []

        def save(value):
            saved.append(value["draft_pr"])

        def create():
            calls.append("created")
            return {"url": "https://github.com/example/repo/pull/1"}

        first = pipeline.create_draft_once(state, save, create)
        second = pipeline.create_draft_once(state, save, create)
        self.assertEqual(first, second)
        self.assertEqual(calls, ["created"])
        self.assertEqual(saved, [first])

    def test_dry_run_never_calls_remote_pr_adapter(self):
        class SyntheticRunner(pipeline.Runner):
            def stage(self, name):
                if name == "differential":
                    result = synthetic_result()
                    pipeline.assert_result_sufficient(result, 2)
                    return {"gcc": result, "clang": result,
                            "compiler_comparison_agrees": True}
                if name == "evidence":
                    return {"promoted": False}
                return {"stage": name}

            def create_draft_pr(self):
                raise AssertionError("dry-run attempted a remote write")

        with tempfile.TemporaryDirectory() as temporary:
            runner = SyntheticRunner.__new__(SyntheticRunner)
            runner.args = SimpleNamespace()
            runner.identity = {"candidate": "synthetic"}
            runner.run_id = "0123456789abcdef"
            runner.private = Path(temporary) / runner.run_id
            runner.state_path = runner.private / "run.json"
            state = runner.run(dry_run=True, open_draft_pr=False)
            self.assertEqual(state["status"], "validated")
            self.assertTrue(state["dry_run"])
            self.assertNotIn("draft_pr", state)


if __name__ == "__main__":
    unittest.main()
