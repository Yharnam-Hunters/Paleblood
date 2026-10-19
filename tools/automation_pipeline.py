#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Opt-in orchestration for a qualified function's reviewable verification run.

This coordinates the existing catalog, differential engine, CMake build, and GitHub CLI. It
does not generate replacement source and never changes function verification status.
"""
from __future__ import annotations

import argparse
import csv
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import differential  # noqa: E402
import function_catalog as catalog  # noqa: E402
import function_qualification as qualification  # noqa: E402

PIPELINE_VERSION = "0.1.1"
STAGES = ("eligibility", "worktree", "fixture_generation", "build", "differential", "validation", "evidence")


class PipelineError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_abi_evidence(references: list, root: Path = ROOT) -> list[str]:
    """Require evidence references that resolve to project files or cite an external URL."""
    if not isinstance(references, list) or not references:
        return ["ABI evidence references are absent"]
    problems = []
    for reference in references:
        if not isinstance(reference, str) or not reference.strip():
            problems.append("ABI evidence reference must be a non-empty string")
            continue
        value = reference.strip()
        if value.startswith("https://"):
            continue
        relative = value.split(":", 1)[0]
        path = (root / relative).resolve()
        if root.resolve() not in path.parents or not path.is_file():
            problems.append(f"ABI evidence does not resolve to a repository file or HTTPS source: {value}")
    return problems


def recorded_provenance_blockers(cases: list[dict], name: str, *, regression_only: bool,
                                 verification_records: list[dict], minimum: int) -> list[str]:
    """Require recorder metadata for new candidates; legacy verified sets need a current record."""
    captured = [case.get("capture") for case in cases]
    if regression_only and not any(captured):
        supported = any(item.get("current") and item.get("cases") == "recorded"
                        and int(item.get("passed", 0)) >= minimum and int(item.get("failed", 0)) == 0
                        for item in verification_records)
        return [] if supported else ["legacy fixture set lacks a current passing recorded-verification record"]
    blockers = []
    for case, provenance in zip(cases, captured):
        if not isinstance(provenance, dict) or provenance.get("function") != name \
                or not isinstance(provenance.get("run_id"), str) or not provenance["run_id"].strip():
            blockers.append(f"fixture {case.get('id', '?')} lacks function/run_id capture provenance")
    return blockers


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def command(argv: list[str], cwd: Path, *, capture: bool = True,
            env: dict | None = None) -> subprocess.CompletedProcess:
    try:
        result = subprocess.run(argv, cwd=cwd, check=False, text=True,
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.PIPE if capture else None, env=env)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise PipelineError(f"could not run {argv[0]}: {error}") from error
    if result.returncode:
        detail = (result.stderr or result.stdout or "").strip().splitlines()
        raise PipelineError(f"command failed ({result.returncode}): {' '.join(argv)}"
                            + ("\n" + "\n".join(detail[-8:]) if detail else ""))
    return result


def load_state(path: Path, identity: dict) -> dict:
    if not path.exists():
        return {"schema": 1, "pipeline_version": PIPELINE_VERSION,
                "identity": identity, "completed": {}, "created_at": int(time.time())}
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PipelineError(f"run state is unreadable: {error}") from error
    if state.get("identity") != identity or state.get("pipeline_version") != PIPELINE_VERSION:
        raise PipelineError("existing run state has different inputs or pipeline version; choose a new run")
    if not isinstance(state.get("completed"), dict):
        raise PipelineError("run state is malformed")
    return state


def execute_stages(state_path: Path, identity: dict, stages: tuple[str, ...], run_stage) -> dict:
    """Persist each completed stage so an interrupted process resumes idempotently."""
    state = load_state(state_path, identity)
    for stage in stages:
        if stage in state["completed"]:
            continue
        result = run_stage(stage)
        state["completed"][stage] = result
        state["last_completed_stage"] = stage
        atomic_json(state_path, state)
    state["status"] = "validated"
    atomic_json(state_path, state)
    return state


def create_draft_once(state: dict, save_state, create_remote) -> dict:
    """Reuse a previously opened draft and persist its URL before returning."""
    if state.get("draft_pr"):
        return state["draft_pr"]
    result = create_remote()
    state["draft_pr"] = result
    state["status"] = "draft_pr_opened"
    save_state(state)
    return result


def assert_result_sufficient(result: dict, minimum_fixtures: int) -> None:
    """Reject missing traces, incomplete/unsupported accesses, and inadequate CFG evidence."""
    comparison = result.get("comparison", {})
    if comparison.get("cases", 0) < minimum_fixtures:
        raise PipelineError(f"insufficient fixture coverage: {comparison.get('cases', 0)} "
                            f"< required {minimum_fixtures}")
    for side in ("original", "replacement"):
        cases = result.get(side, {}).get("cases", [])
        if len(cases) != comparison.get("cases"):
            raise PipelineError(f"{side} execution results are incomplete")
        for case in cases:
            trace = case.get("instruction_trace")
            if not isinstance(trace, dict):
                raise PipelineError(f"{side}/{case.get('id', '?')}: instruction trace is absent")
            if trace.get("complete") is not True:
                gap = (trace.get("gaps") or [{}])[0]
                raise PipelineError(f"{side}/{case.get('id', '?')}: incomplete instruction trace: "
                                    f"{gap.get('reason', trace.get('stop_reason', 'unknown'))}")
            if trace.get("skipped_host_calls") or trace.get("external_transitions") or trace.get("import_observations"):
                raise PipelineError(f"{side}/{case.get('id', '?')}: untraced external execution; "
                                    "boundary observations cannot qualify instruction or CFG coverage")
            coverage = trace.get("control_flow_coverage")
            if not isinstance(coverage, dict) or coverage.get("status") != "complete" \
                    or coverage.get("feedback_eligible") is not True \
                    or not isinstance(coverage.get("blocks"), list) or not coverage["blocks"] \
                    or not isinstance(coverage.get("edges"), list):
                raise PipelineError(f"{side}/{case.get('id', '?')}: complete CFG coverage unavailable")
            events = trace.get("events", [])
            if not isinstance(events, list):
                raise PipelineError(f"{side}/{case.get('id', '?')}: malformed memory-access trace")
            for event in events:
                if not isinstance(event, dict) or event.get("valid_region") is not True \
                        or event.get("supported") is False or not isinstance(event.get("width"), int) \
                        or event["width"] <= 0:
                    raise PipelineError(f"{side}/{case.get('id', '?')}: unsupported or invalid memory access")
                if event.get("access") in ("write", "read_write") and \
                        (event.get("before") is None or event.get("after") is None):
                    raise PipelineError(f"{side}/{case.get('id', '?')}: write trace lacks before/after values")
    if comparison.get("failed", 1) != 0 or comparison.get("passed", 0) != comparison.get("cases"):
        raise PipelineError("original/replacement differential comparison has mismatches")


def compiler_outcomes_agree(gcc: dict, clang: dict) -> bool:
    """Compare observable test outcomes, not compiler-specific diagnostic internals."""
    return all(gcc.get(key) == clang.get(key) for key in ("cases", "passed", "failed"))


def patch_scope_blockers(paths: list[str]) -> list[str]:
    """Keep candidate patches away from the workflow, runtime, and verification gates."""
    allowed_exact = {"README.md", "symbols/functions.csv", "game/hooks.csv"}
    allowed_prefixes = ("game/", "test/", "docs/systems/")
    return [path for path in paths if path not in allowed_exact
            and not path.startswith(allowed_prefixes)]


class Runner:
    def __init__(self, args: argparse.Namespace, identity: dict, run_id: str):
        self.args = args
        self.identity = identity
        self.run_id = run_id
        self.base = args.base
        self.worktree = args.worktree_root / run_id
        self.private = args.output / run_id
        self.normalized = self.private / "fixtures"
        self.state_path = self.private / "run.json"
        self.abi = json.loads(args.abi.read_text(encoding="utf-8")) if args.abi else None

    def stage_eligibility(self) -> dict:
        candidate = differential.catalog_candidate(self.args.state, self.args.address)
        function = candidate["function"]
        decision = candidate.get("qualification") or {}
        name = self.args.name or function.get("name")
        if not name or function.get("name") != name:
            raise PipelineError("candidate name must match the catalog record")
        if self.args.regression_only:
            if self.args.patch or function.get("replacement_status") != "verified":
                raise PipelineError("regression-only mode requires an already verified function and no patch")
            records = function.get("verification", {}).get("records", [])
            if not any(item.get("current") and item.get("cases") == "recorded" and
                       int(item.get("passed", 0)) > 0 and int(item.get("failed", 0)) == 0 for item in records):
                raise PipelineError("regression-only mode requires current passing recorded verification evidence")
            kind = "regression_canary_only"
        else:
            if self.args.patch is None:
                raise PipelineError("full automation requires a prewritten clean-room replacement patch; source generation is unavailable")
            if function.get("replacement_status") not in (None, "unimplemented", "discovered"):
                raise PipelineError("candidate already has a replacement; use regression-only mode for a canary")
            if decision.get("classification") != "eligible_for_replacement_development" \
                    or decision.get("eligible_for_replacement_development") is not True:
                raise PipelineError("catalog does not currently qualify this candidate for replacement development")
            if candidate.get("ownership", {}).get("status") != "confirmed_unclaimed":
                raise PipelineError("candidate ownership is not confirmed unclaimed")
            if not qualification.ownership_snapshot_is_current(
                    qualification.load_ownership(self.args.state)):
                raise PipelineError("ownership snapshot is incomplete or stale; synchronize ownership first")
            kind = "replacement_candidate"
        if not self.args.elf.is_file():
            raise PipelineError(f"local executable is unavailable: {self.args.elf}")
        if not self.args.captures.is_dir():
            raise PipelineError(f"local original-game fixture directory is unavailable: {self.args.captures}")
        blockers = differential.validate_abi(self.abi)
        if blockers:
            raise PipelineError("ABI evidence is insufficient: " + "; ".join(blockers))
        if not any(str(item).strip() for item in self.abi.get("evidence", [])):
            raise PipelineError("ABI evidence references are empty")
        evidence_errors = validate_abi_evidence(self.abi["evidence"])
        if evidence_errors:
            raise PipelineError("ABI evidence references are invalid: " + "; ".join(evidence_errors))
        rows, loaded_abi = differential.load_fixtures(self.args.captures, self.args.address)
        if loaded_abi and loaded_abi != self.abi:
            raise PipelineError("fixture ABI conflicts with the supplied evidence-backed ABI manifest")
        recorded = [(path, case) for path, case in rows if not path.parent.name.lower() in ("edge", "synthetic")]
        if len(recorded) < self.args.minimum_fixtures:
            raise PipelineError(f"insufficient local recorded fixtures: {len(recorded)} < {self.args.minimum_fixtures}")
        provenance_errors = recorded_provenance_blockers(
            [case for _, case in recorded], name, regression_only=self.args.regression_only,
            verification_records=function.get("verification", {}).get("records", []),
            minimum=self.args.minimum_fixtures)
        if provenance_errors:
            raise PipelineError("fixture provenance is incomplete: " + "; ".join(provenance_errors[:5]))
        return {"kind": kind, "address": self.args.address, "name": name,
                "qualification": decision.get("classification", "already_verified_regression_only"),
                "ownership": candidate.get("ownership", {}).get("status", "not_applicable"),
                "ownership_snapshot_sha256": candidate.get("ownership", {}).get("snapshot_sha256"),
                "recorded_fixture_count": len(recorded), "abi_evidence": self.abi["evidence"],
                "executable_sha256": sha256(self.args.elf),
                "catalog_identity": candidate.get("catalog_identity")}

    def stage_worktree(self) -> dict:
        self.args.worktree_root.mkdir(parents=True, exist_ok=True)
        branch = f"codex/auto-{self.run_id}"
        if self.worktree.exists():
            head = command(["git", "rev-parse", "HEAD"], self.worktree).stdout.strip()
            status_lines = command(["git", "status", "--porcelain"], self.worktree).stdout.splitlines()
            status = "\n".join(status_lines)
            if head != self.base:
                raise PipelineError("existing automation worktree is not clean at the recorded base")
            if status:
                if not self.args.patch or any(line.startswith("??") for line in status_lines):
                    raise PipelineError("existing automation worktree has unexpected changes")
                reverse = subprocess.run(["git", "apply", "--reverse", "--check", str(self.args.patch)],
                                          cwd=self.worktree, text=True, stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE)
                if reverse.returncode:
                    raise PipelineError("existing worktree changes do not match the recorded input patch")
        else:
            branch_exists = subprocess.run(["git", "show-ref", "--verify", "--quiet", f"refs/heads/{branch}"], cwd=ROOT)
            if branch_exists.returncode == 0:
                command(["git", "worktree", "add", str(self.worktree), branch], ROOT)
                if command(["git", "rev-parse", "HEAD"], self.worktree).stdout.strip() != self.base:
                    raise PipelineError("existing automation branch does not point at the recorded base")
            else:
                command(["git", "worktree", "add", "-b", branch, str(self.worktree), self.base], ROOT)
        if self.args.patch and not (self.worktree / ".git").exists():
            raise PipelineError("worktree metadata is unavailable")
        if self.args.patch:
            reverse = subprocess.run(["git", "apply", "--reverse", "--check", str(self.args.patch)],
                                      cwd=self.worktree, text=True, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE)
            if reverse.returncode == 0:
                return {"path": str(self.worktree), "branch": branch, "base": self.base,
                        "patch_sha256": sha256(self.args.patch), "resumed_after_patch": True}
            command(["git", "apply", "--check", str(self.args.patch)], self.worktree)
            command(["git", "apply", str(self.args.patch)], self.worktree)
            changed = [line[3:] for line in command(
                ["git", "status", "--porcelain", "--untracked-files=all"], self.worktree).stdout.splitlines()]
            if not changed:
                raise PipelineError("prewritten patch made no changes")
            blocked = patch_scope_blockers(changed)
            if blocked:
                raise PipelineError("candidate patch touches protected paths: " + ", ".join(blocked[:8]))
            if any((self.worktree / path).is_symlink() for path in changed):
                raise PipelineError("candidate patch may not add or alter symlinks")
        self.reject_verification_promotion()
        return {"branch": branch, "base": self.base,
                "patch_sha256": sha256(self.args.patch) if self.args.patch else None}

    def reject_verification_promotion(self) -> None:
        if self.args.regression_only:
            return
        before = command(["git", "show", f"{self.base}:symbols/functions.csv"], self.worktree).stdout
        after = (self.worktree / "symbols/functions.csv").read_text(encoding="utf-8")
        before_rows = list(csv.DictReader(io.StringIO(before)))
        after_rows = list(csv.DictReader(io.StringIO(after)))
        old_verified = sum(row.get("status") == "verified" for row in before_rows)
        new_verified = sum(row.get("status") == "verified" for row in after_rows)
        if new_verified != old_verified:
            raise PipelineError("automation patch may not change the verified-function count")
        for relative in ("symbols/verification.csv", "symbols/reviews.csv"):
            changed = subprocess.run(["git", "diff", "--quiet", self.base, "--", relative],
                                     cwd=self.worktree)
            if changed.returncode:
                raise PipelineError(f"automation patch may not modify {relative}")

    def stage_fixture_generation(self) -> dict:
        self.normalized.mkdir(parents=True, exist_ok=True, mode=0o700)
        if self.normalized.is_symlink():
            raise PipelineError("fixture output directory may not be a symlink")
        os.chmod(self.normalized, 0o700)
        rows, _ = differential.load_fixtures(self.args.captures, self.args.address)
        recorded = [(path, case) for path, case in rows if path.parent.name.lower() not in ("edge", "synthetic")]
        selected = recorded[:self.args.recorded_limit]
        if len(selected) < self.args.minimum_fixtures:
            raise PipelineError("recorded fixture limit is below the required coverage threshold")
        for index, (path, case) in enumerate(selected):
            copied = dict(case)
            copied["abi"] = self.abi
            copied["capture"] = copied.get("capture") or {
                "function": self.args.name, "run_id": path.stem.rsplit("_", 1)[0]}
            differential.atomic_write(self.normalized / f"recorded-{index:05d}.json",
                                      (json.dumps(copied, sort_keys=True, indent=2) + "\n").encode())
        # Generate bounded ABI-guided variants with the existing engine. These are synthetic additions,
        # and are never substituted for the minimum recorded original-game fixture set.
        seed_paths = sorted(self.normalized.glob("recorded-*.json"))
        seed_rows = [(path, json.loads(path.read_text(encoding="utf-8"))) for path in seed_paths]
        generated = differential.explore_fixtures(seed_rows, self.abi, self.args.generated_fixtures,
                                                  self.args.seed) if self.args.generated_fixtures else []
        generated_count = 0
        for case in generated:
            if "exploration" not in case:
                continue
            case["abi"] = self.abi
            differential.atomic_write(self.normalized / f"generated-{case['id']}.json",
                                      (json.dumps(case, sort_keys=True, indent=2) + "\n").encode())
            generated_count += 1
        return {"recorded": len(selected), "generated": generated_count, "seed": self.args.seed,
                "fixture_sha256": hashlib.sha256("".join(
                    sha256(path) for path in sorted(self.normalized.glob("*.json"))).encode()).hexdigest()}

    def stage_build(self) -> dict:
        builds = {}
        for label, cc, cxx in (("gcc", "gcc", "g++"), ("clang", "clang", "clang++")):
            if not shutil.which(cc) or not shutil.which(cxx):
                raise PipelineError(f"both GCC and Clang are required; missing {cc} or {cxx}")
            build = self.worktree / f"build-automation-{label}"
            command(["cmake", "-S", str(self.worktree), "-B", str(build),
                     "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_COMPILER={cc}",
                     f"-DCMAKE_CXX_COMPILER={cxx}"], self.worktree)
            command(["cmake", "--build", str(build), "--parallel", str(self.args.jobs)], self.worktree)
            candidates = list(build.rglob("libbbgame.so"))
            if len(candidates) != 1:
                raise PipelineError(f"{label} replacement library could not be identified unambiguously")
            builds[label] = {"build_directory": str(build), "library_sha256": sha256(candidates[0]),
                             "library": str(candidates[0])}
        return {label: {"library_sha256": data["library_sha256"],
                        "build_directory": data["build_directory"],
                        "library": data["library"]}
                for label, data in builds.items()}

    def stage_differential(self) -> dict:
        results = {}
        for label, build in self.state["completed"]["build"].items():
            env = os.environ.copy()
            env["BB_LOADER"] = str(Path(build["build_directory"]) / "runtime/libpbloader.so")
            command_args = [sys.executable, str(self.worktree / "tools/differential.py"),
                            "--state", str(self.args.state), "--json", "differential", self.args.address,
                            "--cases", str(self.normalized), "--elf", str(self.args.elf),
                            "--function", self.args.name, "--lib", build["library"], "--resume",
                            "--trace-instructions", "--trace-instruction-limit", str(self.args.trace_limit)]
            completed = command(command_args, self.worktree, env=env)
            try:
                result = json.loads(completed.stdout)
            except json.JSONDecodeError as error:
                raise PipelineError(f"{label} differential engine did not return valid JSON") from error
            assert_result_sufficient(result, self.args.minimum_fixtures)
            results[label] = result
        gcc = results["gcc"]["comparison"]
        clang = results["clang"]["comparison"]
        if not compiler_outcomes_agree(gcc, clang):
            raise PipelineError("GCC and Clang differential outcomes disagree")
        return {**results, "compiler_comparison_agrees": True}

    def stage_validation(self) -> dict:
        commands = [
            ["ctest", "--test-dir", str(self.worktree / f"build-automation-{compiler}"), "--output-on-failure"]
            for compiler in ("gcc", "clang")
        ] + [
            [sys.executable, "-m", "unittest", "discover", "-s", "test", "-p", "test_*.py"],
            [sys.executable, "tools/validate_functions.py"],
            [sys.executable, "tools/progress.py", "--check"],
            [sys.executable, "tools/check_no_game_data.py", "--tracked"],
            [sys.executable, "tools/check_agnostic.py"],
            [sys.executable, "tools/check_readable.py"],
            [sys.executable, "tools/verify.py", "--self-test"],
            ["bash", "tools/end_session.sh", "--audit"],
            ["bash", "tools/test_hooks.sh"],
        ]
        results = []
        temporary_root = self.private / "tmp"
        temporary_root.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(temporary_root, 0o700)
        env = os.environ.copy()
        env["TMPDIR"] = str(temporary_root)
        env["BB_LOADER"] = str(self.worktree / "build-automation-gcc/runtime/libpbloader.so")
        env["BB_PBBOOT"] = str(self.worktree / "build-automation-gcc/runtime/pbboot")
        for argv in commands:
            completed = command(argv, self.worktree, env=env)
            label = "ctest-gcc" if argv[0] == "ctest" and "build-automation-gcc" in argv[2] else (
                "ctest-clang" if argv[0] == "ctest" else Path(argv[1]).name if len(argv) > 1 else argv[0])
            results.append({"check": label, "exit_code": completed.returncode})
        return {"passed": len(results), "commands": results}

    def stage_evidence(self) -> dict:
        comparison_summary = {}
        for compiler in ("gcc", "clang"):
            differential_run = self.state["completed"]["differential"][compiler]
            feedback = differential_run.get("coverage_feedback", {}).get("by_side", {})
            comparison_summary[compiler] = {
                "passed": differential_run["comparison"].get("passed"),
                "cases": differential_run["comparison"].get("cases"),
                "failed": differential_run["comparison"].get("failed"),
                "coverage": {side: {"status": record.get("status"),
                                    "fixtures": record.get("feedback_fixture_count", 0),
                                    "blocks": len(record.get("block_coverage", {})),
                                    "edges": len(record.get("edge_coverage", {}))}
                             for side, record in feedback.items()}}
        report = {"schema": 1, "pipeline_version": PIPELINE_VERSION, "run_id": self.run_id,
                  "automated_at": int(time.time()), "identity": self.identity,
                  "eligibility": self.state["completed"]["eligibility"],
                  "worktree": {"branch": self.state["completed"]["worktree"]["branch"],
                               "base": self.state["completed"]["worktree"]["base"]},
                  "fixtures": self.state["completed"]["fixture_generation"],
                  "build_sha256": {label: data["library_sha256"]
                                   for label, data in self.state["completed"]["build"].items()},
                  "differential": comparison_summary,
                  "validation": self.state["completed"]["validation"],
                  "automation": {"replacement_generation": "not automated; accepts a prewritten patch",
                                 "function_status_promotion": "never automated",
                                 "merge": "never automated", "wiki": "not modified"}}
        evidence = self.private / "evidence.json"
        atomic_json(evidence, report)
        markdown = ["# Paleblood automation run", "", f"- Run ID: `{self.run_id}`",
                    f"- Candidate: `{self.args.name}` at `{self.args.address}`",
                    f"- Mode: `{report['eligibility']['kind']}`",
                    f"- Recorded original-game fixtures: {report['fixtures']['recorded']}",
                    f"- Additional generated fixtures: {report['fixtures']['generated']}",
                  f"- Differential: GCC and Clang each passed "
                    f"{report['differential']['gcc']['passed']}/"
                    f"{report['differential']['gcc']['cases']} fixtures",
                    "- Function verification status changed: no", "- Replacement generation automated: no",
                    "", "This report contains fixture identifiers and aggregate results only; private fixture bodies are not exported.", ""]
        (self.private / "evidence.md").write_text("\n".join(markdown), encoding="utf-8")
        return {"json": str(evidence), "markdown": str(self.private / "evidence.md"),
                "sha256": sha256(evidence)}

    def stage(self, name: str) -> dict:
        return getattr(self, f"stage_{name}")()

    def run(self, *, dry_run: bool, open_draft_pr: bool) -> dict:
        self.private.mkdir(parents=True, exist_ok=True, mode=0o700)
        if self.private.is_symlink():
            raise PipelineError("private output directory may not be a symlink")
        os.chmod(self.private, 0o700)
        lock = self.private / "run.lock"
        with lock.open("a") as stream:
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise PipelineError("this run is already active in another process") from error
            self.state = load_state(self.state_path, self.identity)
            for name in STAGES:
                if name in self.state["completed"]:
                    continue
                result = self.stage(name)
                self.state["completed"][name] = result
                self.state["last_completed_stage"] = name
                atomic_json(self.state_path, self.state)
            self.state["status"] = "validated"
            self.state["dry_run"] = dry_run
            atomic_json(self.state_path, self.state)
            if open_draft_pr:
                if dry_run:
                    raise PipelineError("--open-draft-pr cannot be combined with --dry-run")
                if self.args.regression_only:
                    raise PipelineError("regression canaries cannot create function PRs")
                result = create_draft_once(self.state, lambda state: atomic_json(self.state_path, state),
                                           self.create_draft_pr)
            return self.state

    def create_draft_pr(self) -> dict:
        status = command(["git", "status", "--porcelain"], self.worktree).stdout.strip()
        if status:
            command(["git", "add", "-A"], self.worktree)
            command(["git", "-c", "user.name=Codex", "-c", "user.email=codex@openai.com",
                     "commit", "-m", f"decomp: prepare {self.args.name} candidate",
                     "-m", "Finding: differential evidence passed the configured bounded gates.",
                     "-m", "Co-Authored-By: Codex <noreply@openai.com>"], self.worktree)
        branch = self.state["completed"]["worktree"]["branch"]
        verify_summary = self.private / "verify-summary.json"
        differential_result = self.state["completed"]["differential"]["gcc"]
        atomic_json(verify_summary, {"function": self.args.name,
                                    "cases": differential_result["comparison"]["cases"],
                                    "passed": differential_result["comparison"]["passed"],
                                    "failed": differential_result["comparison"]["failed"]})
        report = command([sys.executable, str(self.worktree / "tools/session_report.py"),
                          "--base", self.base, "--verify", str(verify_summary),
                          "--captures", str(self.normalized)], self.worktree).stdout
        body_path = self.private / "pull-request-body.md"
        body_path.write_text("### What\n\nBounded automated candidate workflow; see evidence summary.\n\n"
                             "### verify.py output\n\nThe Phase 2 differential engine matched all configured fixtures. "
                             "This does not promote the function to verified.\n\n"
                             "### Mutation test\n\nMutation requirements remain part of independent review.\n\n"
                             + report, encoding="utf-8")
        command([sys.executable, str(self.worktree / "tools/session_report.py"),
                 "--check-body", str(body_path)], self.worktree)
        command(["git", "push", "-u", "origin", branch], self.worktree)
        existing = subprocess.run(["gh", "pr", "list", "--head", branch, "--state", "all",
                                   "--json", "number,url,state"], cwd=self.worktree,
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if existing.returncode:
            raise PipelineError("could not check for an existing PR; refusing duplicate creation")
        rows = json.loads(existing.stdout)
        if rows:
            return {"url": rows[0]["url"], "number": rows[0]["number"], "reused": True}
        created = command(["gh", "pr", "create", "--draft", "--base", "main", "--head", branch,
                           "--title", f"decomp: prepare {self.args.name} candidate",
                           "--body-file", str(body_path)], self.worktree)
        return {"url": created.stdout.strip(), "reused": False}


def run_id_for(identity: dict) -> str:
    return hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:16]


def compiler_version(command_name: str) -> str:
    executable = shutil.which(command_name)
    if not executable:
        raise PipelineError(f"required compiler is unavailable: {command_name}")
    result = subprocess.run([executable, "--version"], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise PipelineError(f"cannot identify compiler version: {command_name}")
    return (result.stdout or result.stderr).splitlines()[0]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("address")
    parser.add_argument("--name", required=True, help="must match the catalog function name")
    parser.add_argument("--state", type=Path, required=True, help="existing Phase 1 SQLite catalog")
    parser.add_argument("--elf", type=Path, required=True, help="locally held target ELF; never copied")
    parser.add_argument("--captures", type=Path, required=True, help="private original-game fixture directory")
    parser.add_argument("--abi", type=Path, required=True, help="evidence-backed ABI JSON manifest")
    parser.add_argument("--patch", type=Path, help="prewritten clean-room git patch; source generation is unavailable")
    parser.add_argument("--regression-only", action="store_true", help="allow a verified function only as a non-promoting canary")
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--worktree-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="private evidence/state directory outside repository")
    parser.add_argument("--recorded-limit", type=int, default=64)
    parser.add_argument("--generated-fixtures", type=int, default=16)
    parser.add_argument("--minimum-fixtures", type=int, default=32)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--trace-limit", type=int, default=100_000)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--dry-run", action="store_true", help="perform local validation only; no push or PR creation")
    parser.add_argument("--open-draft-pr", action="store_true", help="push and open/reuse a draft PR after all gates pass")
    args = parser.parse_args(argv)
    if args.recorded_limit < args.minimum_fixtures or args.minimum_fixtures < 1:
        parser.error("--recorded-limit must be >= --minimum-fixtures >= 1")
    if args.generated_fixtures < 0 or args.jobs < 1 or args.trace_limit < 1:
        parser.error("fixture count, jobs, and trace limit must be nonnegative/positive")
    if args.dry_run and args.open_draft_pr:
        parser.error("--dry-run cannot be combined with --open-draft-pr")
    if args.regression_only and args.open_draft_pr:
        parser.error("regression-only runs cannot create function PRs")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.state.is_file() or not args.elf.is_file():
        print("catalog state or local executable is unavailable", file=sys.stderr)
        return 2
    if args.abi is None or not args.abi.is_file():
        print("an explicit ABI manifest is required", file=sys.stderr)
        return 2
    if args.patch and not args.patch.is_file():
        print("prewritten patch is unavailable", file=sys.stderr)
        return 2
    try:
        differential.ensure_private_output(args.worktree_root)
        differential.ensure_private_output(args.output)
        differential.ensure_private_output(args.state)
        differential.ensure_private_output(args.elf)
        differential.ensure_private_output(args.captures)
        differential.ensure_private_output(args.abi)
        if args.patch:
            differential.ensure_private_output(args.patch)
            args.patch = args.patch.resolve()
    except differential.DifferentialError as error:
        print(f"private input/output location required: {error}", file=sys.stderr)
        return 2
    root = ROOT
    base_result = subprocess.run(["git", "rev-parse", args.base], cwd=root, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if base_result.returncode:
        print(f"base revision is unavailable: {args.base}", file=sys.stderr)
        return 2
    args.base = base_result.stdout.strip()
    if args.open_draft_pr:
        main_result = subprocess.run(["git", "rev-parse", "origin/main"], cwd=ROOT, text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if main_result.returncode or main_result.stdout.strip() != args.base:
            print("draft PR creation requires the worktree base to be the current origin/main", file=sys.stderr)
            return 2
    patch_sha = sha256(args.patch) if args.patch else None
    try:
        db = catalog.connect(args.state)
        try:
            metadata = {key: json.loads(value) for key, value in db.execute("SELECT key,value FROM metadata")}
            catalog_identity = metadata.get("catalog_identity")
        finally:
            db.close()
    except (OSError, ValueError) as error:
        print(f"catalog identity is unavailable: {error}", file=sys.stderr)
        return 2
    if not catalog_identity:
        print("catalog identity is unavailable; rebuild the Phase 1 catalog", file=sys.stderr)
        return 2
    artifact_paths = {"functions": ROOT / "symbols/functions.csv",
                      "ghidra_functions": ROOT / "symbols/ghidra_functions.csv",
                      "verification": ROOT / "symbols/verification.csv"}
    artifact_hashes = metadata.get("artifacts", {})
    for key, path in artifact_paths.items():
        if key not in artifact_hashes or catalog.sha256(path) != artifact_hashes[key]:
            print(f"catalog is stale for {path.relative_to(ROOT)}; rebuild before automation", file=sys.stderr)
            return 2
    if metadata.get("executable_sha256") != sha256(args.elf):
        print("catalog executable SHA-256 does not match --elf", file=sys.stderr)
        return 2
    try:
        candidate = differential.catalog_candidate(args.state, args.address)
        ownership_snapshot = qualification.load_ownership(args.state)
    except (differential.DifferentialError, ValueError) as error:
        print(f"candidate qualification is unavailable: {error}", file=sys.stderr)
        return 2
    qualification_identity = hashlib.sha256(json.dumps(candidate.get("qualification"), sort_keys=True,
                                                        separators=(",", ":")).encode()).hexdigest()
    ownership_identity = hashlib.sha256(json.dumps(ownership_snapshot, sort_keys=True,
                                                   separators=(",", ":")).encode()).hexdigest()
    try:
        catalog_address = catalog.address(args.address)
    except ValueError as error:
        print(f"invalid candidate address: {error}", file=sys.stderr)
        return 2
    identity = {"address": catalog_address, "name": args.name,
                "base": args.base, "elf_sha256": sha256(args.elf),
                "captures_sha256": hashlib.sha256("".join(sha256(path) for path in sorted(args.captures.glob("*.json"))).encode()).hexdigest(),
                "abi_sha256": sha256(args.abi), "patch_sha256": patch_sha,
                "pipeline_source_sha256": sha256(Path(__file__).resolve()),
                "catalog_identity": catalog_identity, "regression_only": args.regression_only,
                "qualification_sha256": qualification_identity,
                "ownership_snapshot_sha256": ownership_identity,
                "seed": args.seed, "minimum_fixtures": args.minimum_fixtures,
                "recorded_limit": args.recorded_limit,
                "generated_fixtures": args.generated_fixtures,
                "trace_limit": args.trace_limit,
                "compilers": {name: compiler_version(name) for name in ("gcc", "g++", "clang", "clang++")}}
    run_id = run_id_for(identity)
    try:
        state = Runner(args, identity, run_id).run(dry_run=args.dry_run, open_draft_pr=args.open_draft_pr)
    except (PipelineError, differential.DifferentialError, ValueError, OSError) as error:
        print(f"automation blocked: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"run_id": run_id, "status": state.get("status"),
                      "completed_stages": list(state["completed"]), "draft_pr": state.get("draft_pr"),
                      "evidence": state.get("completed", {}).get("evidence")}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
