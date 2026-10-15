#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run bounded, reproducible comparisons through Paleblood's existing guest harness.

This command orchestrates tools/harness.py and tools/verify.py. It is not a guest emulator and
does not promote function status; only the established review and verification workflow can do so.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import shlex
import sqlite3
import subprocess
import sys
import time
import uuid
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import function_catalog as catalog  # noqa: E402
import function_qualification as qualification  # noqa: E402
import verify  # noqa: E402
import verification  # noqa: E402

SCHEMA = 1
ENGINE_VERSION = "1.2.0"
ABI_REQUIRED_FIELDS = {"calling_convention", "confidence", "evidence", "stack_alignment",
                       "arguments", "returns", "input_regions", "writable_regions", "execution_limits"}


class DifferentialError(ValueError):
    pass


def canonical(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def digest_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def digest_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise DifferentialError(f"{path.name}: {error}") from error
    if not isinstance(value, dict):
        raise DifferentialError(f"{path.name}: fixture must be a JSON object")
    return value


def validate_abi(abi: dict | None) -> list[str]:
    if not isinstance(abi, dict):
        return ["ABI manifest is absent; calling convention and argument layout are unknown"]
    missing = sorted(ABI_REQUIRED_FIELDS - set(abi))
    blockers = [f"ABI manifest lacks {field}" for field in missing]
    if abi.get("calling_convention") != "sysv_amd64":
        blockers.append("only explicit sysv_amd64 fixture ABI is currently supported")
    if abi.get("confidence") != "supported":
        blockers.append("ABI confidence is not marked supported by evidence")
    if not isinstance(abi.get("evidence"), list) or not abi.get("evidence"):
        blockers.append("ABI manifest needs evidence references")
    if not isinstance(abi.get("arguments"), list) or not isinstance(abi.get("returns"), dict):
        blockers.append("ABI argument and return descriptions are malformed")
    else:
        seen = set()
        registers = {"rdi", "rsi", "rdx", "rcx", "r8", "r9", "xmm0", "xmm1"}
        for index, argument in enumerate(abi["arguments"]):
            if not isinstance(argument, dict):
                blockers.append(f"ABI argument {index} is not an object")
                continue
            register = argument.get("register")
            stack_index = argument.get("stack_index")
            if register not in registers and not isinstance(stack_index, int):
                blockers.append(f"ABI argument {index} needs a supported register or stack_index")
            if isinstance(stack_index, int) and stack_index < 0:
                blockers.append(f"ABI argument {index} stack_index must be nonnegative")
            location = ("register", register) if register else ("stack", stack_index)
            if location in seen:
                blockers.append(f"ABI argument location is duplicated: {location[1]}")
            seen.add(location)
            if not isinstance(argument.get("type"), str) or not argument["type"].strip():
                blockers.append(f"ABI argument {index} needs an evidence-backed type")
            if argument.get("direction") not in ("input", "output", "inout"):
                blockers.append(f"ABI argument {index} needs an input/output/inout direction")
        if not isinstance(abi["returns"].get("type"), str) or not abi["returns"]["type"].strip():
            blockers.append("ABI return type is unknown")
    for field in ("input_regions", "writable_regions"):
        if not isinstance(abi.get(field), list) or any(not isinstance(item, str) or not item for item in abi[field]):
            blockers.append(f"{field} must be a list of declared buffer names")
    if abi.get("stack_alignment") != 16:
        blockers.append("fixture stack alignment must explicitly be 16 bytes")
    if not isinstance(abi.get("writable_regions"), list) or not isinstance(abi.get("execution_limits"), dict):
        blockers.append("writable regions and execution limits must be explicit")
    limits = abi.get("execution_limits")
    if isinstance(limits, dict):
        timeout = limits.get("timeout_seconds")
        if not isinstance(timeout, int) or timeout < 1 or timeout > 3600:
            blockers.append("execution_limits.timeout_seconds must be an integer from 1 to 3600")
    return blockers


def effective_timeout(requested: int | None, abi: dict) -> int:
    limit = int(abi["execution_limits"]["timeout_seconds"])
    if requested is None:
        return limit
    if requested < 1 or requested > limit:
        raise DifferentialError(f"--timeout must be between 1 and the fixture limit ({limit}) seconds")
    return requested


def load_fixtures(directory: Path, address: str) -> tuple[list[tuple[Path, dict]], dict | None]:
    if not directory.is_dir():
        raise DifferentialError(f"fixture directory does not exist: {directory}")
    cases = []
    for path in sorted(directory.glob("*.json")):
        case = read_json(path)
        if case.get("schema") != verify.SCHEMA:
            raise DifferentialError(f"{path.name}: unsupported fixture schema")
        if catalog.address(str(case.get("address", ""))) != catalog.address(address):
            raise DifferentialError(f"{path.name}: fixture address does not match {address}")
        if not isinstance(case.get("args", {}), dict) or not isinstance(case.get("buffers", {}), dict):
            raise DifferentialError(f"{path.name}: args and buffers must be objects")
        validate_fixture_case(case, path.name)
        if case.get("abi") is not None:
            blockers = validate_abi(case["abi"])
            if blockers:
                raise DifferentialError(f"{path.name}: " + "; ".join(blockers))
        cases.append((path, case))
    if not cases:
        raise DifferentialError(f"no JSON fixtures in {directory}")
    manifest_path = directory / "abi.json"
    abi = read_json(manifest_path) if manifest_path.is_file() else next(
        (case["abi"] for _, case in cases if case.get("abi")), None)
    if abi:
        declared = {item.get("register") for item in abi.get("arguments", []) if isinstance(item, dict)}
        for path, case in cases:
            case_args = set(case.get("args", {}))
            unexpected = case_args - declared - {"xmm0", "xmm1"}
            if unexpected:
                raise DifferentialError(f"{path.name}: argument registers lack ABI declarations: {sorted(unexpected)}")
            missing_arguments = []
            for argument in abi.get("arguments", []):
                register, stack_index = argument.get("register"), argument.get("stack_index")
                if register and register not in case_args:
                    missing_arguments.append(register)
                elif stack_index is not None and stack_index >= len(case.get("stack", [])):
                    missing_arguments.append(f"stack[{stack_index}]")
            if missing_arguments:
                raise DifferentialError(f"{path.name}: fixture omits declared ABI arguments: {missing_arguments}")
            missing_regions = set(abi.get("writable_regions", [])) - set(case.get("buffers", {}))
            if missing_regions:
                raise DifferentialError(f"{path.name}: ABI writable buffers are missing: {sorted(missing_regions)}")
            missing_inputs = set(abi.get("input_regions", [])) - set(case.get("buffers", {}))
            if missing_inputs:
                raise DifferentialError(f"{path.name}: ABI input buffers are missing: {sorted(missing_inputs)}")
            abi_return = abi.get("returns", {}).get("type")
            result_type = {"void": "void", "int8_t": "i8", "uint8_t": "i8", "int32_t": "i32",
                           "uint32_t": "i32", "int64_t": "i64", "uint64_t": "i64", "float": "f32",
                           "double": "f64", "__int128": "i128", "vec128": "vec"}.get(abi_return)
            if result_type and case.get("returns", "i64") != result_type:
                raise DifferentialError(f"{path.name}: fixture return type disagrees with ABI manifest")
    return cases, abi


def validate_fixture_case(case: dict, name: str) -> None:
    """Reject nondeterministic expressions and fixture memory writes outside declared buffers."""
    buffers = case.get("buffers", {})
    for buffer_name, spec in buffers.items():
        if not isinstance(spec, dict) or not isinstance(spec.get("size"), int) or spec["size"] < 0:
            raise DifferentialError(f"{name}: buffer {buffer_name} needs a nonnegative integer size")
        try:
            data = bytes.fromhex(spec.get("bytes", ""))
        except (TypeError, ValueError) as error:
            raise DifferentialError(f"{name}: buffer {buffer_name} has malformed bytes") from error
        if len(data) > spec["size"]:
            raise DifferentialError(f"{name}: initial bytes exceed buffer {buffer_name}")

    def reference(value: object, where: str) -> None:
        if isinstance(value, int):
            return
        if not isinstance(value, str):
            raise DifferentialError(f"{name}: {where} must be a fixed integer or address")
        if value.startswith("buf:"):
            region, _, offset = value[4:].partition("+")
            if region not in buffers:
                raise DifferentialError(f"{name}: {where} refers to unknown buffer {region}")
            try:
                parsed = int(offset, 0) if offset else 0
            except ValueError as error:
                raise DifferentialError(f"{name}: {where} has invalid buffer offset") from error
            if parsed < 0 or parsed > buffers[region]["size"]:
                raise DifferentialError(f"{name}: {where} points outside buffer {region}")
            return
        if value.startswith("guest:"):
            try:
                int(value[6:], 0)
            except ValueError as error:
                raise DifferentialError(f"{name}: {where} has an invalid guest address") from error
            return
        try:
            int(value, 0)
        except ValueError as error:
            raise DifferentialError(f"{name}: {where} is nondeterministic or unsupported: {value!r}") from error

    for register, value in case.get("args", {}).items():
        reference(value, f"args.{register}")
    for index, value in enumerate(case.get("stack", [])):
        reference(value, f"stack[{index}]")
    for index, entry in enumerate(case.get("memory", [])):
        if not isinstance(entry, dict) or "addr" not in entry:
            raise DifferentialError(f"{name}: memory[{index}] needs an address")
        reference(entry["addr"], f"memory[{index}].addr")
        payloads = sum(key in entry for key in ("bytes", "pointer", "guest"))
        if payloads != 1:
            raise DifferentialError(f"{name}: memory[{index}] needs exactly one deterministic value")
        if "bytes" in entry:
            try:
                data = bytes.fromhex(entry["bytes"])
            except (TypeError, ValueError) as error:
                raise DifferentialError(f"{name}: memory[{index}] has malformed bytes") from error
            target = entry["addr"]
            if isinstance(target, str) and target.startswith("buf:"):
                region, _, offset = target[4:].partition("+")
                start = int(offset, 0) if offset else 0
                if start + len(data) > buffers[region]["size"]:
                    raise DifferentialError(f"{name}: memory[{index}] writes beyond buffer {region}")
        elif "pointer" in entry and entry["pointer"] not in buffers:
            raise DifferentialError(f"{name}: memory[{index}] points to unknown buffer")


def executable_identity(path: Path) -> str:
    if not path.is_file():
        raise DifferentialError(f"executable unavailable: {path}; provide --elf or set BB_ELF")
    return digest_file(path)


def catalog_candidate(state: Path, address: str) -> dict:
    try:
        tools = qualification.qualification_tool_identity(ROOT)
        records = qualification.load_qualifications(state, tools)
        db = catalog.connect(state)
        try:
            row = db.execute("SELECT data FROM functions WHERE address=?", (catalog.address(address),)).fetchone()
            metadata_row = db.execute("SELECT value FROM metadata WHERE key='catalog_identity'").fetchone()
            metadata = json.loads(metadata_row[0]) if metadata_row else None
        finally:
            db.close()
    except (sqlite3.Error, OSError) as error:
        raise DifferentialError(f"catalog unavailable: {error}") from error
    if row is None:
        raise DifferentialError(f"{catalog.address(address)} is absent from the local Phase 1 catalog")
    decision = next((record for record in records if record.get("address") == catalog.address(address)), None)
    return {"function": json.loads(row[0]), "catalog_identity": metadata,
            "qualification": decision}


def source_identity(name: str) -> dict:
    """Return public, repository-relative replacement source hashes without local paths."""
    try:
        _, replacement = verify.lookup(name)
    except verify.BadInput as error:
        raise DifferentialError(str(error)) from error
    funcs = {row["name"]: row for row in csv.DictReader((ROOT / "symbols/functions.csv").open(newline=""))}
    row = funcs[name]
    files = verification_files(name)
    if not files:
        raise DifferentialError(f"no source file defines bb_{name}")
    return {"name": name, "address": row["address"], "replacement": replacement,
            "source_sha256": catalog.replacement_source_hash(str(ROOT), name),
            "files": [{"path": path.relative_to(ROOT).as_posix(), "sha256": digest_file(path)} for path in files]}


def verification_files(name: str) -> list[Path]:
    token = f"bb_{name}"
    out = []
    for path in (ROOT / "game").rglob("*"):
        if path.suffix not in (".c", ".cc", ".cpp"):
            continue
        try:
            if token in path.read_text(encoding="utf-8", errors="replace"):
                out.append(path)
        except OSError:
            continue
    return sorted(out)


def ensure_private_output(path: Path) -> None:
    resolved = path.resolve()
    if resolved == ROOT or ROOT in resolved.parents:
        raise DifferentialError("evidence and fixture output must stay outside the public repository")


def database(path: Path) -> sqlite3.Connection:
    db = catalog.connect(path)
    db.execute("""CREATE TABLE IF NOT EXISTS differential_runs (
        run_id TEXT PRIMARY KEY, address TEXT NOT NULL, cache_key TEXT NOT NULL,
        created_at INTEGER NOT NULL, result TEXT NOT NULL)""")
    db.execute("""CREATE TABLE IF NOT EXISTS differential_cases (
        cache_key TEXT NOT NULL, side TEXT NOT NULL, fixture_id TEXT NOT NULL, result TEXT NOT NULL,
        PRIMARY KEY(cache_key, side, fixture_id))""")
    db.execute("""CREATE TABLE IF NOT EXISTS verification_state (
        address TEXT PRIMARY KEY, cache_key TEXT NOT NULL, updated_at INTEGER NOT NULL,
        fixture_status TEXT NOT NULL, original_status TEXT NOT NULL,
        differential_status TEXT NOT NULL, coverage_status TEXT NOT NULL,
        mismatch_status TEXT NOT NULL, blockers TEXT NOT NULL, evidence_ref TEXT NOT NULL)""")
    return db


def update_verification_state(state: Path, address: str, cache_key: str, *,
                              fixture_status: str = "unknown", original_status: str = "not_run",
                              differential_status: str = "not_run", coverage_status: str = "unavailable",
                              mismatch_status: str = "unknown", blockers: list[str] | None = None,
                              evidence_ref: str = "") -> None:
    db = database(state)
    try:
        db.execute("BEGIN IMMEDIATE")
        db.execute("INSERT INTO verification_state VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                   "ON CONFLICT(address) DO UPDATE SET cache_key=excluded.cache_key, "
                   "updated_at=excluded.updated_at,fixture_status=excluded.fixture_status, "
                   "original_status=excluded.original_status,differential_status=excluded.differential_status, "
                   "coverage_status=excluded.coverage_status,mismatch_status=excluded.mismatch_status, "
                   "blockers=excluded.blockers,evidence_ref=excluded.evidence_ref",
                   (catalog.address(address), cache_key, int(time.time()), fixture_status, original_status,
                    differential_status, coverage_status, mismatch_status,
                    canonical(blockers or []), evidence_ref))
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


def run_identity(address: str, elf_sha: str, fixture_hash: str, abi: dict | None,
                 replacement: dict | None, qualification_record: dict | None = None,
                 catalog_identity: dict | None = None,
                 build_config: dict | None = None,
                 instruction_trace: dict | None = None) -> dict:
    return {"schema": SCHEMA, "engine_version": ENGINE_VERSION,
            "engine_sha256": digest_file(ROOT / "tools/differential.py"),
            "harness_sha256": digest_file(ROOT / "tools/harness.py"),
            "instruction_trace_sha256": digest_file(ROOT / "tools/instruction_trace.py"),
            "verify_sha256": digest_file(ROOT / "tools/verify.py"),
            "loader_sha256": digest_file(ROOT / "runtime/loader.c"),
            "executable_sha256": elf_sha, "address": catalog.address(address),
            "fixture_sha256": fixture_hash, "abi_sha256": digest_bytes(canonical(abi).encode()) if abi else None,
            "replacement": replacement,
            "qualification_sha256": digest_bytes(canonical(qualification_record).encode())
            if qualification_record else None,
            "catalog_identity": catalog_identity, "build_configuration": build_config,
            "instruction_trace": instruction_trace}


def build_configuration(library: Path) -> dict:
    """Read reproducible, path-free CMake build identity for a project-built replacement."""
    cache = library.resolve().parent.parent / "CMakeCache.txt"
    values = {}
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("//") or line.startswith("#") or ":" not in line:
                continue
            key, remainder = line.split(":", 1)
            _, _, value = remainder.partition("=")
            if key in {"CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
                       "CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS", "CMAKE_SHARED_LINKER_FLAGS"}:
                values[key] = value
    compiler_names = {key: os.path.basename(value) for key, value in values.items()
                      if key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER")}
    flag_hashes = {key: digest_bytes(value.encode()) for key, value in values.items()
                   if key.endswith(("_C_FLAGS", "_CXX_FLAGS", "_LINKER_FLAGS"))}
    return {"cmake_cache_available": cache.is_file(),
            "build_type": values.get("CMAKE_BUILD_TYPE", ""),
            "compiler_commands": compiler_names, "flag_sha256": flag_hashes}


def save_run(state: Path, identity: dict, result: dict) -> str:
    cache_key = digest_bytes(canonical(identity).encode())
    run_id = cache_key[:16]
    db = database(state)
    try:
        db.execute("BEGIN IMMEDIATE")
        db.execute("INSERT INTO differential_runs VALUES (?, ?, ?, ?, ?) "
                   "ON CONFLICT(run_id) DO UPDATE SET cache_key=excluded.cache_key, "
                   "created_at=excluded.created_at, result=excluded.result",
                   (run_id, identity["address"], cache_key, int(time.time()), canonical({"identity": identity,
                                                                                          "result": result})))
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()
    return run_id


def current_run(state: Path, address: str, run_id: str | None = None) -> dict:
    db = database(state)
    try:
        if run_id:
            row = db.execute("SELECT result FROM differential_runs WHERE run_id=? AND address=?",
                             (run_id, catalog.address(address))).fetchone()
        else:
            row = db.execute("SELECT result FROM differential_runs WHERE address=? ORDER BY created_at DESC LIMIT 1",
                             (catalog.address(address),)).fetchone()
    finally:
        db.close()
    if not row:
        raise DifferentialError("no differential evidence is stored for this address")
    return json.loads(row[0])


def run_original(elf: Path, cases_dir: Path, address: str, timeout: int) -> dict:
    command = [sys.executable, str(ROOT / "tools/harness.py"), "--elf", str(elf),
               "--cases", str(cases_dir), "--address", catalog.address(address), "--timeout", str(timeout)]
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=timeout * len(list(cases_dir.glob("*.json"))) + 30)
    except subprocess.TimeoutExpired as error:
        raise DifferentialError("original harness exceeded the total campaign timeout") from error
    if result.returncode:
        raise DifferentialError(f"original harness failed ({result.returncode}): {result.stderr.strip()[-600:]}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise DifferentialError(f"original harness returned invalid JSON: {error}") from error


def cached_case(state: Path, cache_key: str, side: str, fixture_id: str) -> dict | None:
    db = database(state)
    try:
        row = db.execute("SELECT result FROM differential_cases WHERE cache_key=? AND side=? AND fixture_id=?",
                         (cache_key, side, fixture_id)).fetchone()
        return json.loads(row[0]) if row else None
    finally:
        db.close()


def save_case(state: Path, cache_key: str, side: str, fixture_id: str, result: dict) -> None:
    db = database(state)
    try:
        db.execute("INSERT INTO differential_cases VALUES (?, ?, ?, ?) "
                   "ON CONFLICT(cache_key,side,fixture_id) DO UPDATE SET result=excluded.result",
                   (cache_key, side, fixture_id, canonical(result)))
        db.commit()
    finally:
        db.close()


def run_differential(name: str, elf: Path, fixture_rows: list[tuple[Path, dict]], lib: Path,
                     timeout: int, state: Path, cache_key: str, resume: bool,
                     trace_instructions: bool = False, trace_limit: int = 100_000) -> dict:
    address, symbol = verify.lookup(name)
    sides = {"original": [], "replacement": []}
    for path, _case in fixture_rows:
        fixture_id = path.stem
        for side, replacement_symbol in (("original", None), ("replacement", symbol)):
            observed = cached_case(state, cache_key, side, fixture_id) if resume else None
            if observed is None:
                observed = verify.run_side(str(elf), str(path), address, replacement_symbol,
                                           str(lib) if replacement_symbol else None, timeout=timeout,
                                           trace_instructions=trace_instructions, trace_limit=trace_limit)
                save_case(state, cache_key, side, fixture_id, observed)
            observed["id"] = fixture_id
            sides[side].append(observed)
    original = {"schema": verify.SCHEMA, "address": address, "cases": sides["original"]}
    replacement = {"schema": verify.SCHEMA, "address": address, "cases": sides["replacement"]}
    comparison = verify.compare(original, replacement)
    return {"function": name, "address": address, "original": original,
            "replacement": replacement, "comparison": comparison}


def mismatch_kinds(diffs: list[str]) -> frozenset[str]:
    return frozenset(item.split(".", 1)[0].split(":", 1)[0] for item in diffs)


def minimize_fixture(case: dict, evaluate, max_attempts: int = 64) -> tuple[dict, int]:
    """Greedy bounded delta reduction; retain the original mismatch class and full input elsewhere."""
    if max_attempts < 1 or max_attempts > 1000:
        raise DifferentialError("minimization attempts must be between 1 and 1000")
    current = json.loads(canonical(case))
    baseline = evaluate(current)
    if not baseline:
        raise DifferentialError("fixture does not reproduce a mismatch")
    target = mismatch_kinds(baseline)
    attempts = 0

    def try_value(candidate: dict) -> bool:
        nonlocal current, attempts
        if attempts >= max_attempts:
            return False
        attempts += 1
        diffs = evaluate(candidate)
        if diffs and mismatch_kinds(diffs) == target:
            current = candidate
            return True
        return False

    for key in ("stubs", "imports", "memory"):
        for index in range(len(current.get(key, [])) - 1, -1, -1):
            candidate = json.loads(canonical(current))
            candidate[key].pop(index)
            try_value(candidate)
            if attempts >= max_attempts:
                return current, attempts
    for name, spec in sorted(current.get("buffers", {}).items()):
        raw = bytes.fromhex(spec.get("bytes", ""))
        for index, byte in enumerate(raw):
            if not byte:
                continue
            candidate = json.loads(canonical(current))
            changed = bytearray(bytes.fromhex(candidate["buffers"][name].get("bytes", "")))
            changed[index] = 0
            candidate["buffers"][name]["bytes"] = changed.hex()
            try_value(candidate)
            if attempts >= max_attempts:
                return current, attempts
    for section in ("args",):
        for key, value in sorted(current.get(section, {}).items()):
            if not isinstance(value, str) or value.startswith(("buf:", "guest:")):
                continue
            try:
                number = int(value, 0)
            except ValueError:
                continue
            for replacement in ("0x0", "0x1", "0xffffffffffffffff"):
                if number == int(replacement, 0):
                    continue
                candidate = json.loads(canonical(current))
                candidate[section][key] = replacement
                try_value(candidate)
                if attempts >= max_attempts:
                    return current, attempts
    return current, attempts


def explore_fixtures(fixtures: list[tuple[Path, dict]], abi: dict, limit: int, seed: int) -> list[dict]:
    """Generate bounded typed-value, explicit-alias, and floating-point variants.

    Pointer aliasing is generated only when both ABI arguments explicitly opt in with
    ``alias_safe: true``. This avoids inventing valid alias behavior for unknown contracts.
    """
    if limit < 1 or limit > 10000:
        raise DifferentialError("fixture limit must be between 1 and 10000")
    generated = []
    for path, base in fixtures:
        generated.append(json.loads(canonical(base)))
        for argument in abi.get("arguments", []):
            register = argument.get("register")
            value_type = argument.get("type", "")
            if register not in base.get("args", {}):
                continue
            choices = []
            if value_type.endswith("*"):
                if argument.get("nullable") is True:
                    choices = ["0x0"]
                elif argument.get("alignment") and argument["alignment"] > 1:
                    current = base["args"][register]
                    if isinstance(current, str) and current.startswith("buf:"):
                        choices = [current + "+0x1"]
            elif value_type in ("float", "double"):
                import struct
                if value_type == "float":
                    packed = [struct.pack("<f", value) for value in
                              (0.0, -0.0, 1.0, -1.0, 2.0 ** -149, 3.4028234663852886e38,
                               float("inf"), float("-inf"), float("nan"))]
                else:
                    packed = [struct.pack("<d", value) for value in
                              (0.0, -0.0, 1.0, -1.0, 2.0 ** -1074, 1.7976931348623157e308,
                               float("inf"), float("-inf"), float("nan"))]
                choices = [value.hex().ljust(32, "0") for value in packed]
            else:
                import re
                match = re.fullmatch(r"(?:u?int|u?)(8|16|32|64)_t", value_type)
                if not match:
                    continue
                width = int(match.group(1))
                signed = value_type.startswith("int")
                values = ([0, 1, (1 << (width - 1)) - 1, -(1 << (width - 1)), -1]
                          if signed else [0, 1, (1 << width) - 1])
                mask = (1 << width) - 1
                choices = [hex(value & mask) for value in values]
            for index, value in enumerate(choices):
                variant = json.loads(canonical(base))
                variant["id"] = f"{base.get('id', path.stem)}_explore_{register}_{index}"
                variant["args"][register] = value
                variant["exploration"] = {"seed": seed, "source_fixture": path.stem,
                                           "register": register, "value": value}
                generated.append(variant)
        pointer_args = [item for item in abi.get("arguments", [])
                        if item.get("type", "").endswith("*") and item.get("alias_safe") is True
                        and item.get("register") in base.get("args", {})]
        for left_index, left in enumerate(pointer_args):
            left_value = base["args"][left["register"]]
            if not isinstance(left_value, str) or not left_value.startswith("buf:"):
                continue
            for right in pointer_args[left_index + 1:]:
                right_value = base["args"][right["register"]]
                if not isinstance(right_value, str) or not right_value.startswith("buf:"):
                    continue
                variant = json.loads(canonical(base))
                variant["id"] = f"{base.get('id', path.stem)}_explore_alias_{left['register']}_{right['register']}"
                variant["args"][right["register"]] = left_value
                variant["exploration"] = {"seed": seed, "source_fixture": path.stem,
                                           "alias": [left["register"], right["register"]],
                                           "value": left_value}
                generated.append(variant)
    # Seed affects reproducible ordering when a caller imposes a smaller budget.
    random.Random(seed).shuffle(generated)
    return generated[:limit]


def bundle_markdown(bundle: dict) -> str:
    identity = bundle["identity"]
    result = bundle["result"]
    if result.get("kind") == "original_only":
        execution = result.get("execution", {})
        cases = list(execution.values()) if isinstance(execution, dict) else []
        faults = sum("fault" in case or "errors" in case for case in cases)
        outcome = f"original only: {len(cases)} execution(s), {faults} fault/error result(s); no replacement comparison"
    else:
        summary = result.get("comparison", result)
        outcome = f"{summary.get('passed', 0)} passed, {summary.get('failed', 0)} failed"
    limitations = bundle["limitations"]
    return ("# Differential evidence\n\n"
            f"- Address: `{identity['address']}`\n"
            f"- Executable SHA-256: `{identity['executable_sha256']}`\n"
            f"- Fixture SHA-256: `{identity['fixture_sha256']}`\n"
            f"- Result: {outcome}\n"
            f"- Run identity: `{bundle['run_id']}`\n\n"
            "## Limits\n\n" + "\n".join(f"- {item}" for item in limitations) + "\n")


def print_result(payload: dict, as_json: bool) -> None:
    if as_json:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    if "comparison" in payload:
        summary = payload["comparison"]
        print(f"{payload.get('function', payload.get('address'))}: {summary['passed']}/{summary['cases']} fixtures matched")
        print(f"run {payload.get('run_id', 'not stored')}")
        for failure in summary.get("failures", [])[:10]:
            print(f"  {failure['case']}: {'; '.join(failure['diffs'])}")
    elif "qualification" in payload and "blocked" in payload:
        decision = payload.get("qualification") or {}
        print(f"{payload.get('name') or 'unnamed'} {payload['address']}: {decision.get('classification', 'blocked')}")
        for reason in decision.get("blockers", []):
            print(f"  blocker: {reason}")
    elif "fixture_count" in payload:
        print(f"{payload['address']}: {payload['fixture_count']} fixture(s)")
        for reason in payload.get("abi_blockers", []):
            print(f"  blocked: {reason}")
    elif "generated" in payload:
        print(f"{payload['address']}: generated {payload['generated']} bounded fixtures (seed {payload['seed']})")
    elif "run_id" in payload and "output" in payload:
        print(f"evidence bundle: {payload['output']} (run {payload['run_id']})")
    elif "runs" in payload:
        print(f"stored differential runs: {payload['runs']}")
        for reason in payload.get("blockers", []):
            print(f"  engine limitation: {reason}")
    elif "attempts" in payload:
        print(f"{payload['address']}: minimized after {payload['attempts']} attempts; mismatch preserved: {payload['mismatch_preserved']}")
        for diff in payload.get("diffs", []):
            print(f"  {diff}")
    else:
        for key, value in payload.items():
            if key not in ("result",):
                print(f"{key}: {value}")


def write_bundle(destination: Path, stored: dict, run_id: str) -> None:
    ensure_private_output(destination)
    if len(run_id) != 16 or any(char not in "0123456789abcdef" for char in run_id):
        raise DifferentialError("evidence run ID must be 16 lowercase hexadecimal characters")
    identity, result = stored["identity"], stored["result"]
    bundle = {"schema": SCHEMA, "run_id": run_id, "identity": identity, "result": result,
              "tool_versions": {"python": platform.python_version(), "engine": ENGINE_VERSION,
                                "platform": platform.system() + " " + platform.machine()},
              "compilers": compiler_identity(identity.get("build_configuration")),
              "coverage": {"available": False, "reason": "harness does not emit instruction or CFG coverage"},
              "memory_access_trace": {"available": "scripted_only",
                                      "reason": "harness-mediated import/stub accesses are recorded; guest loads/stores are not instrumented"},
              "memory_reads": {"available": False,
                               "reason": "arbitrary guest memory reads are not dynamically traced"},
              "first_divergence": {"available": False,
                                   "reason": "harness compares final observations; it does not trace instruction-by-instruction state"},
              "limitations": ["Redzones detect changed guest writes into buffer padding, but not reads, same-value writes, or the exact access instruction.",
                             "No instruction-level first-divergence trace or control-flow coverage is emitted.",
                             "Alias cases require explicit alias_safe ABI evidence; exploration does not use coverage feedback.",
                             "A differential result is not a verified-function promotion or proof of real-game behavior.",
                             "Gameplay fixture definitions are private machine data; keep this bundle outside public repositories and do not upload it."]}
    destination.mkdir(parents=True, exist_ok=True)
    temporary = destination / f".bundle-{uuid.uuid4().hex}.tmp"
    temporary.mkdir()
    try:
        (temporary / "evidence.json").write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (temporary / "README.md").write_text(bundle_markdown(bundle), encoding="utf-8")
        final = destination / run_id
        if final.exists():
            shutil.rmtree(temporary)
            return
        temporary.replace(final)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def compiler_identity(build_config: dict | None = None) -> dict:
    configured = (build_config or {}).get("compiler_commands", {})
    fallback = shlex.split(os.environ.get("CC", "cc")) or ["cc"]
    commands = sorted(set(configured.values())) or [fallback[0]]
    versions = {}
    for command in commands:
        resolved = shutil.which(command)
        if not resolved:
            versions[command] = "unavailable"
            continue
        try:
            result = subprocess.run([resolved, "--version"], capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            versions[command] = "unavailable"
            continue
        first_line = (result.stdout or result.stderr).splitlines()
        versions[command] = first_line[0][:240] if first_line else "unavailable"
    return versions


def atomic_write(path: Path, content: bytes) -> None:
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("xb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--state", type=Path, default=catalog.default_state_path(), help="existing Phase 1 SQLite catalog")
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("candidate", "fixtures", "explore", "original", "differential", "minimize", "evidence"):
        child = sub.add_parser(command)
        child.add_argument("address")
        child.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
        if command in ("fixtures", "explore", "original", "differential", "minimize"):
            child.add_argument("--cases", type=Path, required=True)
        if command == "explore":
            child.add_argument("--out", type=Path, required=True)
            child.add_argument("--max-fixtures", type=int, default=64)
            child.add_argument("--seed", type=int, default=0)
        if command in ("original", "differential", "minimize"):
            child.add_argument("--elf", type=Path, default=Path(os.environ.get("BB_ELF", "")))
            child.add_argument("--timeout", type=int)
        if command in ("differential", "minimize"):
            child.add_argument("--function", required=True)
            child.add_argument("--lib", type=Path, default=Path(os.environ.get("BB_GAME_LIB", "")))
        if command == "differential":
            child.add_argument("--resume", action="store_true", help="reuse completed sides from the same evidence key")
            child.add_argument("--trace-instructions", action="store_true",
                               help="trace bounded native guest/replacement memory events (Linux + Capstone 5)")
            child.add_argument("--trace-instruction-limit", type=int, default=100_000)
        if command == "minimize":
            child.add_argument("--fixture", required=True)
            child.add_argument("--max-attempts", type=int, default=64)
            child.add_argument("--out", type=Path, required=True)
        if command == "evidence":
            child.add_argument("--run-id")
            child.add_argument("--out", type=Path, required=True)
    sub.add_parser("status")
    sub.add_parser("blockers")
    sub.choices["status"].add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    sub.choices["blockers"].add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        if args.command in ("status", "blockers"):
            db = database(args.state)
            try:
                records = [json.loads(row[0]) for row in db.execute(
                    "SELECT result FROM differential_runs ORDER BY created_at DESC")]
            finally:
                db.close()
            payload = {"runs": len(records), "latest": [record.get("identity", {}).get("address") for record in records[:20]],
                       "blockers": ["dynamic memory reads", "byte-precise memory-access bounds",
                                    "instruction-level first-divergence trace", "control-flow coverage",
                                    "systematic pointer-alias exploration", "systematic floating-point edge exploration"]
                       if args.command == "blockers" else []}
        elif args.command == "candidate":
            candidate = catalog_candidate(args.state, args.address)
            record = candidate["qualification"]
            payload = {"address": catalog.address(args.address), "name": candidate["function"].get("name"),
                       "catalog_identity": candidate["catalog_identity"],
                       "qualification": record,
                       "blocked": not record or not record.get("eligible_for_replacement_development", False),
                       "reason": "fresh ABI and semantic evidence are required before replacement development"}
        elif args.command == "fixtures":
            fixtures, abi = load_fixtures(args.cases, args.address)
            abi_blockers = validate_abi(abi)
            fixture_hash = digest_bytes("".join(digest_file(path) for path, _ in fixtures).encode())
            fixture_key = digest_bytes(canonical({"address": catalog.address(args.address),
                                                  "fixture_sha256": fixture_hash,
                                                  "abi_sha256": digest_bytes(canonical(abi).encode())
                                                  if abi else None,
                                                  "engine_sha256": digest_file(Path(__file__))}).encode())
            update_verification_state(args.state, args.address, fixture_key,
                                      fixture_status="blocked" if abi_blockers else "validated",
                                      blockers=abi_blockers)
            payload = {"address": catalog.address(args.address), "fixture_count": len(fixtures),
                       "fixtures": [{"id": case.get("id", path.stem), "sha256": digest_file(path),
                                     "provenance": "gameplay" if case.get("capture") else "synthetic_or_edge"}
                                    for path, case in fixtures],
                       "abi": abi, "abi_blockers": abi_blockers}
        elif args.command == "explore":
            ensure_private_output(args.out)
            fixtures, abi = load_fixtures(args.cases, args.address)
            blockers = validate_abi(abi)
            if blockers:
                raise DifferentialError("fixture exploration blocked: " + "; ".join(blockers))
            generated = explore_fixtures(fixtures, abi, args.max_fixtures, args.seed)
            args.out.mkdir(parents=True, exist_ok=True)
            for case in generated:
                identifier = str(case.get("id", ""))
                if not identifier or not all(char.isalnum() or char in "_.-" for char in identifier):
                    raise DifferentialError("fixture ID may contain only letters, digits, dot, dash, and underscore")
                atomic_write(args.out / f"{identifier}.json",
                             (json.dumps(case, indent=2, sort_keys=True) + "\n").encode())
            payload = {"address": catalog.address(args.address), "seed": args.seed,
                       "generated": len(generated), "output": str(args.out),
                       "coverage_note": "bounded typed-value, explicitly safe alias, and float-edge generation; no dynamic coverage feedback"}
        elif args.command == "original":
            elf_sha = executable_identity(args.elf)
            fixtures, abi = load_fixtures(args.cases, args.address)
            blockers = validate_abi(abi)
            if blockers:
                raise DifferentialError("original execution blocked: " + "; ".join(blockers))
            timeout = effective_timeout(args.timeout, abi)
            candidate = catalog_candidate(args.state, args.address)
            function_status = candidate["function"].get("replacement_status")
            if not (candidate.get("qualification") or {}).get("eligible_for_original_execution") and function_status not in (
                    "replaced", "edge-verified", "verified"):
                raise DifferentialError("catalog candidate is not currently eligible for original execution")
            result = run_original(args.elf, args.cases, args.address, timeout)
            fixture_hash = digest_bytes("".join(digest_file(path) for path, _ in fixtures).encode())
            result_data = {"execution": result, "fixture_count": len(fixtures),
                           "fixture_definitions": [case for _, case in fixtures],
                           "capture_provenance": "gameplay" if any(case.get("capture") for _, case in fixtures) else "synthetic_or_edge",
                           "kind": "original_only"}
            identity = run_identity(args.address, elf_sha, fixture_hash, abi, None,
                                    candidate.get("qualification"), candidate.get("catalog_identity"))
            run_id = save_run(args.state, identity, result_data)
            failed_original = any("fault" in case or case.get("errors") for case in result.get("cases", []))
            update_verification_state(args.state, args.address,
                                      digest_bytes(canonical(identity).encode()),
                                      fixture_status="loaded",
                                      original_status="failed" if failed_original else "passed",
                                      blockers=["replacement comparison not run", "control-flow coverage unavailable"],
                                      evidence_ref=run_id)
            payload = {"address": catalog.address(args.address), "executable_sha256": elf_sha,
                       "run_id": run_id, "fixtures": len(fixtures), "result": result,
                       "note": "original instructions only; no replacement comparison"}
        elif args.command == "differential":
            if args.trace_instruction_limit < 1 or args.trace_instruction_limit > 10_000_000:
                raise DifferentialError("--trace-instruction-limit must be between 1 and 10000000")
            elf_sha = executable_identity(args.elf)
            if not args.lib.is_file():
                raise DifferentialError(f"replacement library unavailable: {args.lib}; build the project or pass --lib")
            fixtures, abi = load_fixtures(args.cases, args.address)
            blockers = validate_abi(abi)
            if blockers:
                raise DifferentialError("differential execution blocked: " + "; ".join(blockers))
            timeout = effective_timeout(args.timeout, abi)
            replacement = source_identity(args.function)
            if replacement["address"] != catalog.address(args.address):
                raise DifferentialError("function registry address differs from requested address")
            replacement["library_sha256"] = digest_file(args.lib)
            build_config = build_configuration(args.lib)
            fixture_hash = digest_bytes("".join(digest_file(path) for path, _ in fixtures).encode())
            candidate = catalog_candidate(args.state, args.address)
            function_status = candidate["function"].get("replacement_status")
            if not (candidate.get("qualification") or {}).get("eligible_for_replacement_development") and function_status not in (
                    "replaced", "edge-verified", "verified"):
                raise DifferentialError("candidate is not qualified for replacement development")
            identity = run_identity(args.address, elf_sha, fixture_hash, abi, replacement,
                                    candidate.get("qualification"), candidate.get("catalog_identity"),
                                    build_config,
                                    {"enabled": args.trace_instructions,
                                     "instruction_limit": args.trace_instruction_limit})
            cache_key = digest_bytes(canonical(identity).encode())
            result = run_differential(args.function, args.elf, fixtures, args.lib, timeout,
                                      args.state, cache_key, args.resume,
                                      args.trace_instructions, args.trace_instruction_limit)
            qualification_record = candidate.get("qualification") or {}
            result["qualification"] = {key: qualification_record.get(key) for key in
                                        ("address", "classification", "eligible_for_fixture_generation",
                                         "eligible_for_original_execution", "eligible_for_replacement_development",
                                         "blockers", "ownership_snapshot_sha256", "qualification_cache_key")}
            result["fixture_count"] = len(fixtures)
            result["capture_provenance"] = "gameplay" if any(case.get("capture") for _, case in fixtures) else "synthetic_or_edge"
            result["fixture_definitions"] = [case for _, case in fixtures]
            run_id = save_run(args.state, identity, result)
            comparison = result["comparison"]
            failed_original = any("fault" in case or case.get("errors") for case in result["original"]["cases"])
            update_verification_state(args.state, identity["address"], cache_key,
                                      fixture_status="loaded",
                                      original_status="failed" if failed_original else "passed",
                                      differential_status="failed" if comparison["failed"] else "passed",
                                      mismatch_status="mismatch" if comparison["failed"] else "none",
                                      blockers=([] if args.trace_instructions else
                                                ["instruction-level memory accesses were not enabled"])
                                      + ["unsupported implicit/AVX memory classes make traces incomplete",
                                         "no instruction-aligned register divergence or CFG coverage"],
                                      evidence_ref=run_id)
            payload = {"run_id": run_id, **result}
        elif args.command == "minimize":
            ensure_private_output(args.out)
            target = args.cases / args.fixture
            if not target.is_file() or target.parent.resolve() != args.cases.resolve():
                raise DifferentialError("fixture must be a JSON file directly under --cases")
            elf_sha = executable_identity(args.elf)
            if not args.lib.is_file():
                raise DifferentialError(f"replacement library unavailable: {args.lib}")
            fixture = read_json(target)
            if catalog.address(str(fixture.get("address", ""))) != catalog.address(args.address):
                raise DifferentialError("fixture address differs from requested address")
            abi = fixture.get("abi")
            blockers = validate_abi(abi)
            if blockers:
                raise DifferentialError("minimization blocked: " + "; ".join(blockers))
            timeout = effective_timeout(args.timeout, abi)
            address, symbol = verify.lookup(args.function)
            if address != catalog.address(args.address):
                raise DifferentialError("replacement address differs from requested address")
            original_bytes = target.read_bytes()
            attempts = 0
            with tempfile.TemporaryDirectory(prefix="pb-diff-minimize-") as scratch_root:
                scratch = Path(scratch_root)

                def evaluate(value: dict) -> list[str]:
                    nonlocal attempts
                    attempts += 1
                    case_path = scratch / "candidate.json"
                    case_path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")
                    original = verify.run_side(str(args.elf), str(case_path), address, None, None,
                                               timeout=timeout)
                    replacement_result = verify.run_side(str(args.elf), str(case_path), address,
                                                         symbol, str(args.lib), timeout=timeout)
                    return verify.compare_case(original, replacement_result)

                reduced, _ = minimize_fixture(fixture, evaluate, args.max_attempts)
                final_diffs = evaluate(reduced)
            args.out.mkdir(parents=True, exist_ok=True)
            atomic_write(args.out / "original-fixture.json", original_bytes)
            atomic_write(args.out / "minimized-fixture.json",
                         (json.dumps(reduced, indent=2, sort_keys=True) + "\n").encode())
            payload = {"address": catalog.address(args.address), "executable_sha256": elf_sha,
                       "fixture": target.name, "attempts": attempts,
                       "original_preserved": True, "mismatch_preserved": bool(final_diffs),
                       "diffs": final_diffs}
        elif args.command == "evidence":
            stored = current_run(args.state, args.address, args.run_id)
            run_id = args.run_id or digest_bytes(canonical(stored["identity"]).encode())[:16]
            write_bundle(args.out, stored, run_id)
            payload = {"run_id": run_id, "output": str(args.out / run_id)}
        else:
            raise DifferentialError("unsupported command")
        print_result(payload, getattr(args, "json", False))
        return 0
    except (DifferentialError, verify.BadInput, OSError, sqlite3.Error, ValueError, KeyError) as error:
        print(f"differential: {error}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
