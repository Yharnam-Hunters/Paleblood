# SPDX-License-Identifier: GPL-2.0-or-later
"""Generator protocol and isolation tests. All instruction bytes here are project-owned fixtures."""
from __future__ import annotations

import copy
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import implementation_generator as generator
import automation_pipeline as pipeline
import generation_provider_codex as codex_provider
import test_function_qualification as qualification_fixtures


SOURCE = 'extern "C" uint32_t bb_candidate(uint32_t value) { return value + 1u; }'
# A repository-owned increment fixture, unrelated to any game implementation.
CODE = bytes.fromhex("8d4701c3")


class GeneratorFixture:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.note = self.directory / "abi-notes.txt"
        self.note.write_text("Synthetic increment: one uint32_t argument in RDI, result in EAX.")
        self.elf = self.directory / "fixture.elf"
        self.elf.write_bytes(qualification_fixtures.QualificationTests.make_elf(CODE))
        self.abi = {"calling_convention": "sysv_amd64", "confidence": "supported",
                    "evidence": ["urn:sha256:" + generator.catalog.sha256(self.note)],
                    "arguments": [{"register": "rdi", "type": "uint32_t", "direction": "input"}],
                    "returns": {"type": "uint32_t"}, "stack_alignment": 16,
                    "input_regions": [], "writable_regions": [], "execution_limits": {"timeout_seconds": 2}}
        self.candidate = {"catalog_identity": "synthetic", "ownership": {"status": "confirmed_unclaimed"},
                          "function": {"address": "0x00400100", "size_bytes": len(CODE), "instruction_count": 2,
                                       "replacement_status": "untracked", "calls": {"callers": []}},
                          "qualification": {"boundary_class": "cross_tool_supported",
                                            "eligible_for_original_execution": True,
                                            "executable_sha256": generator.catalog.sha256(self.elf)}}
        self.package = self.make_package()
        self.config = {"kind": "mock", "provider": "unit-test", "model": "fixed-response-not-a-model",
                       "argv": [sys.executable, str(self.directory / "provider.py")],
                       "timeout_seconds": 10, "max_attempts": 1}
        (self.directory / "provider.py").write_text(
            "import json,sys\nr=json.load(sys.stdin)\np=r['analysis']\n"
            + "json.dump({'analysis_sha256':r['analysis_sha256'],'signature':p['signature'],"
            + "'source':" + repr(SOURCE) + ",'assumptions':[], 'evidence':p['abi']['evidence']},sys.stdout)\n")

    def tearDown(self):
        self.temp.cleanup()

    def make_package(self):
        return generator.analysis_package(self.candidate, self.elf, self.abi, [self.note], True)

    def response(self):
        return {"analysis_sha256": generator.digest(self.package), "signature": self.package["signature"],
                "source": SOURCE, "assumptions": [], "evidence": self.abi["evidence"]}


class GeneratorTests(GeneratorFixture, unittest.TestCase):
    def test_analysis_is_complete_and_binds_identity_without_raw_bytes_or_paths(self):
        self.assertEqual(len(self.package["instruction_listing"]), 2)
        self.assertEqual(self.package["mode"], "experimental")
        serialized = json.dumps(self.package)
        self.assertNotIn(str(self.directory), serialized)
        self.assertNotIn('"bytes"', serialized)
        self.assertEqual(generator.validate_response(self.response(), self.package), SOURCE)

    def test_generation_disabled_does_not_invoke_provider_or_write(self):
        destination = self.directory / "disabled"
        with patch.object(generator, "bounded_process", side_effect=AssertionError("provider ran")):
            result = generator.generate(self.package, self.config, destination, enabled=False)
        self.assertEqual(result["status"], "disabled")
        self.assertFalse(destination.exists())

    def test_other_git_worktree_cannot_receive_private_generation_artifacts(self):
        other = self.directory / "unrelated-repository"
        other.mkdir()
        subprocess.run(["git", "init", "-q", str(other)], check=True)
        with self.assertRaisesRegex(generator.GenerationError, "every Git worktree"):
            generator.private_location(other / "analysis" / "output.json")

    def test_rejects_missing_abi_or_unbound_notes(self):
        self.abi["arguments"][0].pop("type")
        with self.assertRaisesRegex(generator.GenerationError, "ABI"):
            self.make_package()
        self.abi["arguments"][0]["type"] = "uint32_t"
        self.note.write_text("changed ABI assumption")
        with self.assertRaisesRegex(generator.GenerationError, "hash-bound"):
            self.make_package()

    def test_provisional_or_pointer_signature_is_not_guessed(self):
        for change in ({"type": "void *"}, {"register": "rdx"}, {"direction": "inout"}):
            with self.subTest(change=change):
                abi = copy.deepcopy(self.abi)
                abi["arguments"][0].update(change)
                with self.assertRaises(generator.GenerationError):
                    generator.scalar_signature(abi)

    def test_contributor_owned_implemented_or_uncertain_boundary_is_rejected(self):
        changes = [("ownership", {"status": "contributor_owned"}),
                   ("qualification", {"boundary_class": "ghidra_only"})]
        for field, value in changes:
            with self.subTest(field=field), self.assertRaises(generator.GenerationError):
                generator.analysis_package({**self.candidate, field: value}, self.elf, self.abi, [self.note], True)
        self.candidate["function"]["replacement_status"] = "verified"
        with self.assertRaisesRegex(generator.GenerationError, "already implemented"):
            self.make_package()

    def test_replacement_admission_is_not_inferred_from_experiment(self):
        with self.assertRaisesRegex(generator.GenerationError, "not admitted"):
            generator.analysis_package(self.candidate, self.elf, self.abi, [self.note], False)

    def test_incomplete_listing_and_unsupported_instructions_fail_closed(self):
        for code in (bytes.fromhex("8d47"), bytes.fromhex("f3a4c3"), bytes.fromhex("8b07c3"),
                     bytes.fromhex("eb00c3"), bytes.fromhex("c390"), bytes.fromhex("66c3")):
            with self.subTest(code=code.hex()), self.assertRaises(generator.GenerationError):
                generator.instruction_listing(code, 0x400100)

    def test_response_rejects_missing_provenance_signature_and_invalid_source(self):
        changes = ({"analysis_sha256": "different"}, {"signature": {}}, {"source": ""},
                   {"evidence": []}, {"assumptions": None}, {"source": "x" * (generator.MAX_SOURCE + 1)},
                   {"source": 'asm("nop");'}, {"source": '#include "/private/input"'})
        for change in changes:
            with self.subTest(change=list(change)), self.assertRaises(generator.GenerationError):
                generator.validate_response({**self.response(), **change}, self.package)

    def test_provider_configuration_is_explicit_and_bounded(self):
        config = self.directory / "config.json"
        config.write_text(json.dumps(self.config))
        self.assertEqual(generator.provider_config(config)["kind"], "mock")
        for change in ({"argv": ["shell command"]}, {"timeout_seconds": 301}, {"max_attempts": 4}, {"kind": "template"}):
            config.write_text(json.dumps({**self.config, **change}))
            with self.assertRaises(generator.GenerationError):
                generator.provider_config(config)

    def test_cli_dry_run_validates_without_provider_or_output(self):
        abi = self.directory / "abi.json"
        abi.write_text(json.dumps(self.abi))
        argv = [self.package["address"], "--state", str(self.directory / "catalog.sqlite"),
                "--elf", str(self.elf), "--abi", str(abi), "--abi-evidence", str(self.note),
                "--output", str(self.directory / "cli-out"), "--experimental", "--dry-run"]
        with patch.object(generator.differential, "catalog_candidate", return_value=self.candidate), \
                patch.object(generator, "bounded_process", side_effect=AssertionError("dry run called provider")), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(generator.main(argv), 0)
        self.assertFalse(json.loads(output.getvalue())["provider_invoked"])
        self.assertFalse((self.directory / "cli-out").exists())

    def test_codex_adapter_rejects_private_input_without_separate_authorization(self):
        request = {"analysis": self.package, "analysis_sha256": generator.digest(self.package)}
        argv = ["--codex", sys.executable, "--model", "explicit-test-model", "--auth-directory", str(self.directory)]
        with patch("sys.stdin", io.StringIO(json.dumps(request))), \
                patch.object(generator, "bounded_process", side_effect=AssertionError("unapproved provider ran")), \
                contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(codex_provider.main(argv), 1)
        self.assertIn("separate explicit", errors.getvalue())

    def test_codex_adapter_uses_structured_ephemeral_read_only_cli(self):
        self.package["evidence_origin"] = "project_owned_synthetic"
        request = {"analysis": self.package, "analysis_sha256": generator.digest(self.package)}
        argv = ["--codex", sys.executable, "--model", "explicit-test-model", "--auth-directory", str(self.directory)]

        def fake_cli(command, directory, _seconds, _input, environment):
            self.assertIn("--ephemeral", command)
            self.assertIn("--ignore-user-config", command)
            self.assertEqual(command[command.index("--sandbox") + 1], "read-only")
            self.assertEqual(command[command.index("--model") + 1], "explicit-test-model")
            self.assertEqual(environment["CODEX_HOME"], str(self.directory))
            (directory / "provider-model-response.json").write_text(json.dumps(self.response()))
            return b""

        with patch("sys.stdin", io.StringIO(json.dumps(request))), patch.object(Path, "cwd", return_value=self.directory), \
                patch.object(generator, "bounded_process", side_effect=fake_cli), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(codex_provider.main(argv), 0)
        self.assertEqual(json.loads(output.getvalue())["source"], SOURCE)

    def test_pipeline_binds_exact_generated_file_and_registry_symbol(self):
        from types import SimpleNamespace
        runner = object.__new__(pipeline.Runner)
        runner.worktree = self.directory
        runner.args = SimpleNamespace(generation_report=self.directory / "generation.json",
                                      generated_source_path="game/example.cpp", name="example")
        source = self.directory / "game/example.cpp"
        source.parent.mkdir()
        source.write_text(SOURCE)
        pipeline.atomic_json(runner.args.generation_report,
                             {"source_sha256": generator.catalog.sha256(source), "symbol": "bb_example"})
        runner.check_generated_source()
        source.write_text("different source")
        with self.assertRaisesRegex(pipeline.PipelineError, "exact generated source"):
            runner.check_generated_source()
        runner.args.generated_source_path = "../outside.cpp"
        with self.assertRaisesRegex(pipeline.PipelineError, "regular game source"):
            runner.check_generated_source()

    def test_resumed_pipeline_rechecks_ownership_freshness_before_reusing_stages(self):
        from types import SimpleNamespace
        args = SimpleNamespace(output=self.directory / "pipeline", worktree_root=self.directory / "worktrees",
                               base="base", abi=None)
        runner = pipeline.Runner(args, {"test": "identity"}, "run")
        state = pipeline.load_state(runner.state_path, runner.identity)
        state["completed"] = {stage: {} for stage in pipeline.STAGES}
        pipeline.atomic_json(runner.state_path, state)
        with patch.object(runner, "stage_eligibility", side_effect=pipeline.PipelineError("ownership expired")):
            with self.assertRaisesRegex(pipeline.PipelineError, "ownership expired"):
                runner.run(dry_run=True, open_draft_pr=False)

    def test_provider_process_protocol_and_timeout(self):
        response = generator.bounded_process(self.config["argv"], self.directory, 5,
            json.dumps({"analysis_sha256": generator.digest(self.package), "analysis": self.package}).encode())
        self.assertEqual(generator.validate_response(json.loads(response), self.package), SOURCE)
        with self.assertRaises(subprocess.TimeoutExpired):
            generator.bounded_process([sys.executable, "-c", "import time; time.sleep(30)"], self.directory, 1)

    def test_interrupted_generation_consumes_budget_without_unbounded_retry(self):
        with patch.object(generator, "bounded_process", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                generator.generate(self.package, self.config, self.directory / "out", enabled=True)
        with self.assertRaisesRegex(generator.GenerationError, "budget exhausted"):
            generator.generate(self.package, self.config, self.directory / "out", enabled=True)

    def test_saved_response_recovers_without_provider_and_tampering_is_rejected(self):
        def interrupted_compile(*_args):
            raise KeyboardInterrupt

        with patch.object(generator, "compile_source", side_effect=interrupted_compile):
            with self.assertRaises(KeyboardInterrupt):
                generator.generate(self.package, self.config, self.directory / "out", enabled=True)
        run, = (self.directory / "out").iterdir()
        # A completed response remains durable even if the generated-stage state write was interrupted.
        state = json.loads((run / "generation.json").read_text())
        state["generated"] = False
        pipeline.atomic_json(run / "generation.json", state)
        with patch.object(generator, "bounded_process", side_effect=AssertionError("provider ran twice")), \
                patch.object(generator, "compile_source", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                generator.generate(self.package, self.config, self.directory / "out", enabled=True)
        (run / "proposal.cpp").write_text("tampered source")
        with self.assertRaisesRegex(generator.GenerationError, "source changed"):
            generator.generate(self.package, self.config, self.directory / "out", enabled=True)

    def test_experimental_mock_cannot_pass_pipeline_admission_even_with_forged_success_flags(self):
        report = {"mode": "experimental", "provider": {"kind": "mock"}, "generated": True, "compiled": True,
                  "address": self.package["address"], "source_sha256": "a" * 64,
                  "identity": {"executable_sha256": self.package["executable_sha256"]},
                  "eligible_for_draft_pr": True, "genuine_original_game_differential_passed": True}
        self.assertIn("experimental/mock", " ".join(pipeline.generation_report_blockers(
            report, self.package["address"], self.package["executable_sha256"])))


class IsolatedGeneratorTests(GeneratorFixture, unittest.TestCase):
    """The same provider is explicitly a mock, never an AI generation result."""
    @classmethod
    def setUpClass(cls):
        if not all(shutil.which(command) for command in ("bwrap", "g++", "clang++")):
            if os.environ.get("BB_REQUIRE_GENERATOR_SANDBOX") == "1":
                raise RuntimeError("required generator sandbox/compiler dependency unavailable")
            raise unittest.SkipTest("sandboxed integration needs bubblewrap, GCC and Clang")
        with tempfile.TemporaryDirectory() as temporary:
            try:
                generator.sandbox(Path(temporary), ["true"], 5)
            except (generator.GenerationError, OSError):
                if os.environ.get("BB_REQUIRE_GENERATOR_SANDBOX") == "1":
                    raise RuntimeError("required generator namespace unavailable; do not skip this CI gate")
                raise unittest.SkipTest("user namespaces unavailable; unsafe execution is not a fallback")

    def test_generated_code_compiles_and_safe_rerun_reuses_provider(self):
        result = generator.generate(self.package, self.config, self.directory / "out", enabled=True)
        self.assertTrue(result["generated"])
        self.assertTrue(result["compiled"])
        self.assertFalse(result["synthetic_test_passed"])
        self.assertFalse(result["genuine_original_game_differential_passed"])
        self.assertFalse(result["eligible_for_draft_pr"])
        with patch.object(generator, "bounded_process", side_effect=AssertionError("completed stage reran")):
            repeated = generator.generate(self.package, self.config, self.directory / "out", enabled=True)
        self.assertEqual(repeated, result)

    def test_invalid_generated_cpp_and_signature_fail_compilation(self):
        for source in ("not c++ code", SOURCE.replace("uint32_t bb_candidate", "uint64_t bb_candidate")):
            with self.subTest(source=source), self.assertRaises(generator.GenerationError):
                generator.compile_source(self.directory, source, self.package["signature"])

    def test_execution_has_no_home_credentials_or_writable_run_state(self):
        sentinel = self.directory / "state.json"
        sentinel.write_text("unchanged")
        script = "from pathlib import Path; assert not Path('/home/spike').exists(); Path('/work/state.json').write_text('bad')"
        with self.assertRaises(generator.GenerationError):
            generator.sandbox(self.directory, ["python3", "-c", script], 5)
        self.assertEqual(sentinel.read_text(), "unchanged")

    def test_synthetic_original_replacement_and_wrong_behavior(self):
        loader = os.environ.get("BB_LOADER")
        if not loader or not Path(loader).is_file():
            self.skipTest("build the loader for the original-instruction regression")
        # Use the existing synthetic PS4 ELF builder and the existing native harness.
        from test_harness import boot_image
        elf = self.directory / "synthetic.elf"
        elf.write_bytes(boot_image())
        # The existing repository-owned fixture at this offset returns the integer seven.
        package = copy.deepcopy(self.package)
        package["address"] = "0x00400900"
        package["executable_sha256"] = generator.catalog.sha256(elf)
        cases = self.directory / "cases"
        cases.mkdir()
        (cases / "abi.json").write_text(json.dumps(self.abi))
        for index, value in enumerate((0, 1, 0xffffffff)):
            (cases / f"fixture-{index}.json").write_text(json.dumps({"schema": 1, "address": package["address"],
                "args": {"rdi": hex(value)}, "returns": "i32", "fixture_generation": {"kind": "synthetic"}}))
        # Suppress the unused-argument warning without inventing a real-game argument contract.
        source = 'extern "C" uint32_t bb_candidate(uint32_t value) { (void)value; return 7u; }'
        generator.compile_source(self.directory, source, package["signature"])
        result = generator.replay(self.directory, package, elf, Path(loader), cases)
        self.assertEqual(result["comparisons"]["g++"]["passed"], 3)
        self.assertEqual(result["comparisons"]["clang++"]["passed"], 3)
        self.assertFalse(result["genuine_original_game_differential_passed"])
        generator.compile_source(self.directory, source.replace("return 7u", "return 8u"), package["signature"])
        with self.assertRaisesRegex(pipeline.PipelineError, "mismatches"):
            generator.replay(self.directory, package, elf, Path(loader), cases)


if __name__ == "__main__":
    unittest.main()
