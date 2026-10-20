#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Opt-in, private implementation proposals; generation never admits a replacement ABI.

The first increment supports bounded, register-only unsigned scalar leaf functions. A trusted,
explicitly configured provider speaks JSON over stdin/stdout. No model or template is built in.
All generated code is compiled and replayed in an offline bubblewrap namespace, never dlopened
in this process. The existing automation pipeline remains the authority for draft-PR eligibility.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import automation_pipeline as pipeline  # noqa: E402
import differential  # noqa: E402
import function_catalog as catalog  # noqa: E402
import function_qualification as qualification  # noqa: E402
import instruction_trace  # noqa: E402
import verify  # noqa: E402

VERSION = "0.1.0"
MAX_SOURCE = 32768
MAX_OUTPUT = 4 * 1024 * 1024
MAX_EXTENT = 256
REGISTERS = ("rdi", "rsi", "rdx", "rcx", "r8", "r9")
SCALAR_TYPES = {"uint32_t", "uint64_t"}
LEAF_MNEMONICS = {"mov", "movzx", "movsxd", "lea", "and", "or", "xor", "add", "sub",
                  "shl", "shr", "sar", "imul", "neg", "not", "ret", "nop"}


class GenerationError(ValueError):
    pass


def digest(value: object) -> str:
    return hashlib.sha256(catalog.canonical(value).encode()).hexdigest()


def private_location(path: Path) -> None:
    """Reject every Git checkout, including another worktree, for private input/output."""
    differential.ensure_private_output(path)
    existing = path.resolve()
    while not existing.is_dir():
        existing = existing.parent
    result = subprocess.run(["git", "-C", str(existing), "rev-parse", "--is-inside-work-tree"],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=5)
    if result.returncode == 0 and result.stdout.strip() == "true":
        raise GenerationError("private analysis, fixtures and output must stay outside every Git worktree")


def scalar_signature(abi: dict) -> dict:
    problems = differential.validate_abi(abi)
    if problems:
        raise GenerationError("ABI evidence is insufficient: " + "; ".join(problems))
    arguments = abi["arguments"]
    if len(arguments) > len(REGISTERS) or any(
            arg.get("register") != REGISTERS[index] or arg.get("type") not in SCALAR_TYPES
            or arg.get("direction") != "input" or "stack_index" in arg
            for index, arg in enumerate(arguments)):
        raise GenerationError("generation supports only ordered unsigned scalar input registers")
    if abi["returns"]["type"] not in SCALAR_TYPES or abi["input_regions"] or abi["writable_regions"]:
        raise GenerationError("generation does not yet support pointers, memory regions or non-integer returns")
    return {"symbol": "bb_candidate", "calling_convention": "sysv_amd64",
            "returns": abi["returns"]["type"], "arguments": [arg["type"] for arg in arguments]}


def instruction_listing(code: bytes, start: int) -> list[dict]:
    """Private textual analysis only. Reject incomplete decode, branches, imports and data memory."""
    if not 0 < len(code) <= MAX_EXTENT:
        raise GenerationError("function extent exceeds the bounded generation limit")
    decoder = instruction_trace.Capstone()
    listing, offset = [], 0
    while offset < len(code):
        try:
            instruction = decoder.decode(start + offset, code[offset:])
        except instruction_trace.TraceUnavailable as error:
            raise GenerationError("incomplete instruction listing") from error
        size = instruction["size"]
        issue = instruction_trace._instruction_support_issue(instruction)
        mnemonic = instruction["mnemonic"]
        special_register = re.search(r"\b(?:[cdefgs]s|[cd]r[0-9]+|[xyz]mm[0-9]+|mm[0-7])\b", instruction["text"])
        if issue or mnemonic not in LEAF_MNEMONICS or instruction["operands"] or special_register:
            raise GenerationError("unsupported generation instruction: " + mnemonic)
        if not 0 < size <= len(code) - offset:
            raise GenerationError("incomplete instruction listing")
        if mnemonic == "ret" and (instruction["text"] or offset + size != len(code)):
            raise GenerationError("ambiguous function boundary or unsupported return")
        listing.append({"address": hex(start + offset), "size": size,
                        "mnemonic": mnemonic, "operands": instruction["text"]})
        offset += size
    if not listing or listing[-1]["mnemonic"] != "ret":
        raise GenerationError("function boundary lacks a terminal return")
    return listing


def analysis_package(candidate: dict, elf: Path, abi: dict, abi_evidence: list[Path],
                     experimental: bool) -> dict:
    """Build from the current executable, catalog decision and separately hash-bound ABI notes."""
    function = candidate["function"]
    decision = candidate.get("qualification") or {}
    if candidate.get("ownership", {}).get("status") != "confirmed_unclaimed":
        raise GenerationError("ownership is not currently confirmed unclaimed")
    if function.get("replacement_status") not in {"original", "untracked"}:
        raise GenerationError("already implemented function cannot enter generation")
    if decision.get("boundary_class") != "cross_tool_supported" \
            or decision.get("eligible_for_original_execution") is not True:
        raise GenerationError("current boundary/original-execution qualification is insufficient")
    if not experimental and decision.get("eligible_for_replacement_development") is not True:
        raise GenerationError("replacement-development ABI is not admitted; use explicit experimental mode")
    signature = scalar_signature(abi)
    if function.get("name"):
        if not re.fullmatch(r"[a-z][a-z0-9_]*", function["name"]):
            raise GenerationError("catalog name is not a supported C identifier")
        signature["symbol"] = "bb_" + function["name"]
    if sum(path.stat().st_size for path in abi_evidence) > 32768:
        raise GenerationError("ABI notes exceed the bounded analysis budget")
    refs = {"urn:sha256:" + catalog.sha256(path): path.read_text(encoding="utf-8")
            for path in abi_evidence}
    if not abi_evidence or any(reference not in refs for reference in abi["evidence"]):
        raise GenerationError("every ABI evidence reference must resolve to supplied hash-bound local notes")
    executable_sha, segments = qualification.read_elf_executable_segments(elf)
    if executable_sha != decision.get("executable_sha256"):
        raise GenerationError("qualification executable identity differs from the current executable")
    start, size = int(function["address"], 16), function.get("size_bytes")
    if not isinstance(size, int) or not 0 < size <= MAX_EXTENT:
        raise GenerationError("function boundary is unknown or exceeds the bounded generation limit")
    segment = next((s for s in segments if s["start"] <= start and start + size <= s["file_end"]), None)
    if segment is None:
        raise GenerationError("function boundary is outside executable file-backed memory")
    with elf.open("rb") as stream:
        stream.seek(segment["offset"] + start - segment["start"])
        code = stream.read(size)
    listing = instruction_listing(code, start)
    if len(listing) != function.get("instruction_count"):
        raise GenerationError("instruction listing disagrees with catalog instruction count")
    return {"schema": 1, "generator_version": VERSION, "mode": "experimental" if experimental else "qualified",
            "address": function["address"], "executable_sha256": executable_sha,
            "boundary": {"classification": decision["boundary_class"], "size": size},
            "instruction_listing": listing, "instruction_sha256": hashlib.sha256(code).hexdigest(),
            "callers": function.get("calls", {}).get("callers", []),
            "type_information": function.get("type_intelligence", {}),
            "abi": abi, "signature": signature, "abi_notes": refs,
            "catalog_identity": candidate["catalog_identity"],
            "qualification_sha256": digest(decision), "ownership": candidate["ownership"],
            "constraints": ["Independently reconstruct behavior; do not reproduce an instruction listing or decompiler output.",
                            "No external implementation code, emulator code, assets or speculative gameplay names.",
                            "Return readable C++ with the exact supplied signature; state all assumptions.",
                            "An experimental proposal is not an admitted ABI or a verified replacement."]}


def provider_config(path: Path) -> dict:
    private_location(path)
    config = differential.read_json(path)
    argv = config.get("argv")
    if not isinstance(argv, list) or not argv or any(not isinstance(a, str) or not a for a in argv) \
            or not Path(argv[0]).is_absolute() or not Path(argv[0]).is_file():
        raise GenerationError("provider argv must start with an explicit absolute executable; no shell is used")
    if config.get("kind") not in {"model", "mock"} or not config.get("provider") or not config.get("model"):
        raise GenerationError("provider, model and explicit kind (model or mock) are required")
    timeout, attempts = config.get("timeout_seconds", 60), config.get("max_attempts", 1)
    if type(timeout) is not int or not 1 <= timeout <= 300 or type(attempts) is not int or not 1 <= attempts <= 3:
        raise GenerationError("provider bounds must be 1..300 seconds and 1..3 attempts")
    return {**config, "timeout_seconds": timeout, "max_attempts": attempts}


def process_limits(seconds: int) -> None:
    os.umask(0o077)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (seconds, seconds + 1))
    # A configured CLI may append to an existing SQLite log larger than stdout's limit.
    # Keep filesystem writes bounded independently; stdout is checked below at MAX_OUTPUT.
    resource.setrlimit(resource.RLIMIT_FSIZE, (128 * 1024**2, 128 * 1024**2))
    resource.setrlimit(resource.RLIMIT_AS, (4 * 1024**3, 4 * 1024**3))


def bounded_process(argv: list[str], directory: Path, seconds: int, input_bytes: bytes = b"",
                    environment: dict | None = None) -> bytes:
    """Bound CPU/address space/output and kill the whole process group on timeout/interruption."""
    env = {"PATH": "/usr/bin:/bin", "LANG": "C", "HOME": str(directory), "TMPDIR": str(directory),
           "PYTHONNOUSERSITE": "1"}
    env.update(environment or {})
    with tempfile.TemporaryFile(dir=directory) as output, tempfile.TemporaryFile(dir=directory) as errors:
        child = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=output, stderr=errors,
                                 cwd=directory, env=env, start_new_session=True,
                                 preexec_fn=lambda: process_limits(seconds))
        try:
            child.communicate(input=input_bytes, timeout=seconds)
        finally:
            # Also remove surviving descendants after a provider exits or an interrupt arrives.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.wait()
        output.seek(0)
        result = output.read(MAX_OUTPUT + 1)
        if len(result) > MAX_OUTPUT:
            raise GenerationError("process output exceeded the bounded output limit")
        if child.returncode:
            errors.seek(0)
            # Full diagnostics stay local; never include provider input in a public report.
            detail = errors.read(4096).decode(errors="replace")
            raise GenerationError(f"bounded process failed ({child.returncode}): {detail}")
        return result


def sandbox(directory: Path, argv: list[str], seconds: int, *, mounts: list[tuple[Path, str]] = (),
            environment: dict | None = None, writable: bool = False) -> bytes:
    """Offline mount/user/PID namespaces. Absence/failure is a blocker, never an unsafe fallback."""
    bwrap = shutil.which("bwrap")
    if not bwrap:
        raise GenerationError("bubblewrap is required; generated code cannot run unsandboxed")
    command = [bwrap, "--unshare-all", "--die-with-parent", "--new-session", "--cap-drop", "ALL",
               "--ro-bind", "/usr", "/usr", "--symlink", "usr/bin", "/bin",
               ]
    # Preserve the host loader layout instead of assuming /lib64 == /usr/lib.
    # Debian/Ubuntu use a multiarch loader link in /lib64; Arch uses /usr/lib.
    for library_root in ("/lib", "/lib64"):
        if Path(library_root).is_dir():
            command.extend(["--ro-bind", library_root, library_root])
    command.extend(["--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--dir", "/work",
               "--bind" if writable else "--ro-bind", str(directory.resolve()), "/work", "--chdir", "/work", "--clearenv",
               "--setenv", "PATH", "/usr/bin:/bin", "--setenv", "HOME", "/tmp",
               "--setenv", "TMPDIR", "/tmp", "--setenv", "PYTHONNOUSERSITE", "1"])
    for source, target in mounts:
        command.extend(["--ro-bind", str(source.resolve()), target])
    for key, value in (environment or {}).items():
        command.extend(["--setenv", key, value])
    return bounded_process(command + argv, directory, seconds)


def validate_response(response: dict, package: dict) -> str:
    if not isinstance(response, dict):
        raise GenerationError("provider response must be a JSON object")
    if response.get("analysis_sha256") != digest(package):
        raise GenerationError("provider response does not bind the supplied analysis package")
    if response.get("signature") != package["signature"]:
        raise GenerationError("provider proposed an unsupported or conflicting signature")
    source = response.get("source")
    if not isinstance(source, str) or not source.strip() or len(source.encode()) > MAX_SOURCE or "\x00" in source:
        raise GenerationError("generated source is absent, invalid or too large")
    if re.search(r"\b(?:asm|__asm|__asm__)\b", source) or any(
            line.lstrip().startswith("#") and not re.fullmatch(r'\s*#\s*include\s*<(?:stdint.h|cstdint)>\s*', line)
            for line in source.splitlines()):
        raise GenerationError("generated source may not embed assembly or arbitrary preprocessor directives")
    if not isinstance(response.get("assumptions"), list) or any(
            not isinstance(a, str) or not a.strip() for a in response["assumptions"]):
        raise GenerationError("provider assumptions must be explicit")
    references = response.get("evidence")
    if not isinstance(references, list) or not references or any(r not in package["abi"]["evidence"] for r in references):
        raise GenerationError("provider evidence references are missing or outside the analysis package")
    return source


def translation_unit(source: str, signature: dict) -> str:
    arguments = ", ".join(signature["arguments"])
    return ("#include <stdint.h>\n" + source + "\n"
            + f"static_assert(__is_same(decltype(&{signature['symbol']}), {signature['returns']}(*)({arguments})));\n"
            + 'extern "C" int bbgame_init(const void *) { return 0; }\n')


def compiler_path(name: str) -> Path:
    configured = shutil.which(name)
    if not configured:
        raise GenerationError("required compiler is unavailable: " + name)
    driver = Path(configured).resolve()
    if Path("/usr") not in driver.parents:
        raise GenerationError("compiler installation is outside the supported read-only /usr toolchain mount")
    return driver


def compile_source(directory: Path, source: str, signature: dict) -> dict:
    (directory / "candidate.cpp").write_text(translation_unit(source, signature), encoding="utf-8")
    results = {}
    for compiler in ("g++", "clang++"):
        # Resolve /etc/alternatives links without exposing the host configuration directory.
        driver = [str(compiler_path(compiler))]
        if compiler == "clang++":
            driver.append("--driver-mode=g++")
        version = sandbox(directory, driver + ["--version"], 10).decode().splitlines()[0]
        sandbox(directory, driver + ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-ffp-contract=off", "-fno-fast-math", "-fPIC", "-shared",
                            "candidate.cpp", "-o", compiler + ".so"], 30, writable=True)
        results[compiler] = {"version": version, "library_sha256": catalog.sha256(directory / (compiler + ".so"))}
    return results


def replay(directory: Path, package: dict, elf: Path, loader: Path, cases: Path) -> dict:
    """Reuse the current original/replacement harness and strict trace comparator inside isolation."""
    rows, abi = differential.load_fixtures(cases, package["address"])
    if abi != package["abi"] or not rows or len(rows) > 64:
        raise GenerationError("replay requires matching ABI and 1..64 deterministic fixtures")
    identity = {"package_sha256": digest(package), "loader_sha256": catalog.sha256(loader),
                "libraries": {c: catalog.sha256(directory / (c + ".so")) for c in ("g++", "clang++")},
                "tools": {name: catalog.sha256(ROOT / "tools" / (name + ".py"))
                          for name in ("harness", "instruction_trace", "verify", "differential", "implementation_generator")},
                "fixture_sha256": digest([(path.name, catalog.sha256(path)) for path, _ in rows])}
    evidence = directory / "replay-evidence" / digest(identity)[:16]
    evidence.mkdir(parents=True, exist_ok=True)
    pipeline.atomic_json(evidence / "identity.json", identity)
    signature = package["signature"]
    # Do not accept extra register, import, global or pointer assumptions via a fixture.
    for _, case in rows:
        expected = "i32" if signature["returns"] == "uint32_t" else "i64"
        if case.get("returns") != expected or set(case.get("args", {})) != set(REGISTERS[:len(signature["arguments"])]) \
                or any(case.get(key) for key in ("buffers", "memory", "imports", "stubs", "observe_registers", "stack_args")):
            raise GenerationError("fixture exceeds the scalar ABI or declared dependency contract")
    mounts = [(ROOT / "tools", "/tools"), (elf, "/executable.elf"), (loader, "/loader.so"), (cases, "/cases")]
    environment = {"BB_LOADER": "/loader.so"}
    try:
        import capstone
    except ImportError:
        pass  # System libcapstone remains supported by the existing decoder.
    else:
        # A pip dependency may live outside /usr (for example a CI toolcache interpreter).
        # Expose this explicit dependency, without mounting an entire home or Python environment.
        mounts.append((Path(capstone.__file__).parent, "/trace-python/capstone"))
        environment["PYTHONPATH"] = "/trace-python"
    timeout = min(package["abi"]["execution_limits"]["timeout_seconds"], 5)
    base = ["python3", "/tools/harness.py", "--elf", "/executable.elf", "--cases", "/cases",
            "--address", package["address"], "--timeout", str(timeout),
            "--trace-instructions", "--trace-instruction-limit", "256"]
    common = {"mounts": mounts, "environment": environment}
    original = json.loads(sandbox(directory, base, 120, **common))
    results = {}
    for compiler in ("g++", "clang++"):
        replacement = json.loads(sandbox(directory, base + ["--replacement", signature["symbol"], "--lib",
                                                           "/work/" + compiler + ".so"], 120, **common))
        sides = {side: {"schema": verify.SCHEMA, "address": package["address"], "cases": list(observed.values())}
                 for side, observed in (("original", original), ("replacement", replacement))}
        result = {**sides, "comparison": verify.compare(sides["original"], sides["replacement"])}
        pipeline.atomic_json(evidence / (compiler + "-differential.json"), result)
        # Missing/extra fixtures must not disappear through batch-result normalization.
        expected_ids = {path.stem for path, _ in rows}
        if set(original) != expected_ids or set(replacement) != expected_ids:
            raise GenerationError("harness returned a different fixture set")
        pipeline.assert_result_sufficient(result, len(rows))
        results[compiler] = result["comparison"]
    if not pipeline.compiler_outcomes_agree(results["g++"], results["clang++"]):
        raise GenerationError("GCC and Clang differential outcomes disagree")
    return {"comparisons": results, "fixture_count": len(rows), "identity": identity,
            "evidence_id": evidence.name,
            "provenance": "constructed_inputs_only", "genuine_original_game_differential_passed": False}


def generate(package: dict, config: dict, output: Path, *, enabled: bool,
             executable: Path | None = None, loader: Path | None = None, cases: Path | None = None) -> dict:
    private_location(output)
    if not enabled:
        return {"status": "disabled", "analysis_sha256": digest(package), "provider_invoked": False}
    identity = {"version": VERSION, "analysis_sha256": digest(package), "provider_config_sha256": digest(config),
                "executable_sha256": package["executable_sha256"],
                "provider_artifacts_sha256": [catalog.sha256(Path(arg)) for arg in config["argv"] if Path(arg).is_file()],
                "generator_sha256": catalog.sha256(Path(__file__)),
                "compiler_versions": {c: pipeline.compiler_version(c) for c in ("g++", "clang++")},
                "compiler_sha256": {c: catalog.sha256(compiler_path(c)) for c in ("g++", "clang++")}}
    directory = output / digest(identity)[:16]
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (directory / "lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        state_path = directory / "generation.json"
        state = differential.read_json(state_path) if state_path.exists() else {
            "schema": 1, "identity": identity, "address": package["address"],
            "mode": package["mode"], "provider": {k: config[k] for k in ("kind", "provider", "model")},
            "symbol": package["signature"]["symbol"],
            "attempts": 0, "generated": False, "compiled": False, "synthetic_test_passed": False,
            "genuine_original_game_differential_passed": False, "eligible_for_draft_pr": False}
        if state["identity"] != identity:
            raise GenerationError("generation state identity changed")
        pipeline.atomic_json(directory / "analysis.json", package)
        if not state["generated"]:
            response_path = directory / "response.json"
            if response_path.exists():
                response = differential.read_json(response_path)
            else:
                if state["attempts"] >= config["max_attempts"]:
                    raise GenerationError("generation attempt budget exhausted; inspect saved state")
                state["attempts"] += 1
                pipeline.atomic_json(state_path, state)
                request = {"schema": 1, "analysis_sha256": digest(package), "analysis": package}
                raw = bounded_process(config["argv"], directory, config["timeout_seconds"],
                                      catalog.canonical(request).encode())
                response = json.loads(raw)
                pipeline.atomic_json(response_path, response)
            source = validate_response(response, package)
            (directory / "proposal.cpp").write_text(source, encoding="utf-8")
            state.update(generated=True, source_sha256=catalog.sha256(directory / "proposal.cpp"),
                         response_sha256=digest(response), assumptions=response["assumptions"])
            pipeline.atomic_json(state_path, state)
        source = (directory / "proposal.cpp").read_text(encoding="utf-8")
        if catalog.sha256(directory / "proposal.cpp") != state["source_sha256"]:
            raise GenerationError("saved generated source changed; refusing stale results")
        if not state["compiled"]:
            state["compilation"] = compile_source(directory, source, package["signature"])
            state["compiled"] = True
            pipeline.atomic_json(state_path, state)
        for compiler, record in state["compilation"].items():
            if catalog.sha256(directory / (compiler + ".so")) != record["library_sha256"]:
                raise GenerationError("compiled library changed; refusing stale results")
        if any(path is not None for path in (executable, loader, cases)):
            state["synthetic_test_passed"] = False
            state.pop("constructed_input_replay", None)
            pipeline.atomic_json(state_path, state)
            if not all(path is not None and path.exists() for path in (executable, loader, cases)):
                raise GenerationError("replay needs executable, loader and fixtures")
            if catalog.sha256(executable) != package["executable_sha256"]:
                raise GenerationError("replay executable identity changed")
            # Rerun replay rather than cache it: harness, loader and fixtures can change independently.
            result = replay(directory, package, executable, loader, cases)
            state["constructed_input_replay"] = result
            state["synthetic_test_passed"] = True
            pipeline.atomic_json(state_path, state)
        state["status"] = "experimental_proposal" if package["mode"] == "experimental" else "proposal_needs_pipeline"
        state["blockers"] = ["Recorded-game provenance and full existing automation gates are still required.",
                             "Generated source needs readable integration and independent human review."]
        pipeline.atomic_json(state_path, state)
        return {**state, "private_run_id": directory.name}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("address")
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--abi", type=Path, required=True)
    parser.add_argument("--abi-evidence", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--experimental", action="store_true", help="proposal only; cannot admit an ABI or become PR eligible")
    parser.add_argument("--provider-config", type=Path)
    parser.add_argument("--enable-generation", action="store_true", help="explicitly authorize the configured provider to receive private analysis")
    parser.add_argument("--dry-run", action="store_true", help="validate private analysis; do not call a provider or compile")
    parser.add_argument("--cases", type=Path, help="optional bounded constructed-input differential regression")
    parser.add_argument("--loader", type=Path)
    args = parser.parse_args(argv)
    try:
        for path in [args.state, args.elf, args.abi, args.output, *args.abi_evidence, args.cases, args.loader]:
            if path:
                private_location(path)
        candidate = differential.catalog_candidate(args.state, catalog.address(args.address))
        package = analysis_package(candidate, args.elf, differential.read_json(args.abi), args.abi_evidence, args.experimental)
        if args.dry_run or not args.enable_generation:
            print(json.dumps({"status": "analysis_ready_generation_disabled", "analysis_sha256": digest(package),
                              "mode": package["mode"], "provider_invoked": False}, indent=2))
            return 0
        if not args.provider_config:
            raise GenerationError("no provider configured; no built-in model or template generation is available")
        config = provider_config(args.provider_config)
        result = generate(package, config, args.output, enabled=True, executable=args.elf if args.cases else None,
                          loader=args.loader, cases=args.cases)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (GenerationError, differential.DifferentialError, pipeline.PipelineError, OSError, ValueError,
            subprocess.TimeoutExpired, instruction_trace.TraceUnavailable) as error:
        print(f"generation blocked: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
