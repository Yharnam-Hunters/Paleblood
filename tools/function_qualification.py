#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ownership evidence and conservative execution qualification for the local catalog."""
from __future__ import annotations

import hashlib
import json
import os
import re
import sqlite3
import struct
import subprocess
import time
from pathlib import Path

import function_catalog as catalog

QUALIFICATION_VERSION = "0.4.0"
OWNERSHIP_MAX_AGE_SECONDS = 24 * 60 * 60
REQUIRED_OWNERSHIP_SOURCES = {"replacement_registry", "repository_ownership_metadata",
                              "open_issues_and_comments", "open_pull_requests", "local_work_assignments"}
HEX_ADDRESS = re.compile(r"(?<![A-Za-z0-9_])0x([0-9a-fA-F]{8,16})(?![A-Za-z0-9_])", re.IGNORECASE)
INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)([^\s]+)?\s*(.*)$")
TERMINATORS = {"ret", "retf", "iret", "iretq", "jmp", "jmpq", "ud2", "int3"}
FORBIDDEN = {"syscall", "sysenter", "int", "int1", "int3", "icebp", "hlt", "in", "out", "cli", "sti",
             "wrmsr", "rdmsr", "ud2", "div", "idiv", "rdtsc", "rdtscp", "cpuid", "rdrand", "rdseed",
             "xgetbv", "xsetbv", "xbegin", "xabort", "monitor", "mwait"}
IMPLICIT_STACK_MEMORY = {"push", "pushq", "pop", "popq", "leave", "enter", "ret", "retq", "retf", "iret", "iretq"}
IMPLICIT_POINTER_MEMORY = {"movs", "movsb", "movsw", "movsd", "movsq", "stos", "stosb", "stosw",
                           "stosd", "stosq", "lods", "lodsb", "lodsw", "lodsd", "lodsq", "cmps",
                           "cmpsb", "cmpsw", "cmpsd", "cmpsq", "scas", "scasb", "scasw", "scasd", "scasq"}


def _run(command: list[str], cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip().splitlines()[-1] if error.stderr.strip() else "no diagnostic"
        raise ValueError(f"ownership source check failed ({command[0]}): {detail}") from error
    return result.stdout


def _safe_claim(claim: dict) -> dict:
    return {key: value for key, value in claim.items()
            if key in {"address", "symbol", "system", "owner", "source", "kind", "id", "url", "checked_at"}}


def _extract_claims(text: str, source: str, owner: str, url: str, names: set[str],
                    active_only: bool = False, addresses: set[str] | None = None) -> list[dict]:
    claims = []
    lines = text.splitlines() if active_only else [text]
    for line in lines:
        if active_only and re.search(r"\[[xX]\]", line):
            continue
        for match in HEX_ADDRESS.finditer(line):
            raw = match.group(1)
            if len(raw) > 16:
                continue
            value = int(raw, 16)
            normalized = catalog.address(hex(value))
            if value <= 0xFFFFFFFFFFFFFFFF and (addresses is None or normalized in addresses):
                claims.append({"address": normalized, "source": source,
                               "owner": owner, "kind": "contributor", "url": url})
        lowered = line.lower()
        for name in names:
            if re.search(rf"(?<![\w]){re.escape(name.lower())}(?![\w])", lowered):
                claims.append({"symbol": name, "source": source, "owner": owner,
                               "kind": "contributor", "url": url})
    return claims


def _local_worktree_evidence(root: Path, names: set[str],
                             candidate_addresses: set[str]) -> tuple[list[dict], dict]:
    listing = _run(["git", "worktree", "list", "--porcelain"], root)
    worktrees: list[dict] = []
    current: dict = {}
    for line in listing.splitlines() + [""]:
        if line.startswith("worktree "):
            current = {"path": Path(line.split(" ", 1)[1])}
        elif line.startswith("HEAD "):
            current["head"] = line.split(" ", 1)[1]
        elif line.startswith("branch "):
            current["branch"] = line.split(" ", 1)[1].removeprefix("refs/heads/")
        elif line == "detached":
            current["detached"] = True
        elif line.startswith("prunable "):
            current["prunable"] = True
        elif not line and current:
            worktrees.append(current)
            current = {}

    claims = []
    summary = []
    base = _run(["git", "rev-parse", "origin/main"], root).strip()
    for item in worktrees:
        path = item["path"]
        branch = item.get("branch", "detached")
        if not path.is_dir():
            if item.get("prunable"):
                summary.append({"branch": branch, "head": item.get("head"), "status": "stale_prunable_registration"})
                continue
            raise ValueError("a non-prunable registered local worktree is unavailable; ownership is incomplete")
        changed = set()
        diff_base = item.get("branch") or item.get("head")
        names_changed = _run(["git", "diff", "--name-only", base, "--", "game/", "symbols/functions.csv"], path)
        changed.update(names_changed.splitlines())
        dirty = _run(["git", "status", "--porcelain", "--", "game/", "symbols/functions.csv"], path)
        changed.update(line[3:].split(" -> ")[-1] for line in dirty.splitlines())
        if "symbols/functions.csv" in changed:
            diff = _run(["git", "diff", "--unified=0", base, "--", "symbols/functions.csv"], path)
            addresses = {catalog.address("0x" + found) for line in diff.splitlines()
                         if line.startswith("+") and not line.startswith("+++")
                         for found in HEX_ADDRESS.findall(line)} & candidate_addresses
            for address in sorted(addresses):
                claims.append({"address": address, "source": "local branch function registry",
                               "owner": "active automation worker", "kind": "local_assignment", "id": branch})
        registry_path = path / "symbols/functions.csv"
        registry = catalog.read_dict_csv(registry_path) if registry_path.is_file() else []
        for relative in sorted(changed):
            if not relative.startswith("game/"):
                continue
            source_path = path / relative
            source_text = source_path.read_text(encoding="utf-8", errors="ignore") if source_path.is_file() else ""
            for row in registry:
                function_name = row.get("name", "")
                suffix = function_name.split("_", 1)[-1]
                if function_name and (suffix in Path(relative).stem or re.search(
                        rf"\bbb_{re.escape(suffix)}\b", source_text)):
                    function_address = catalog.address(row["address"])
                    if function_address in candidate_addresses:
                        claims.append({"address": function_address, "symbol": function_name,
                                       "source": "local game-source worktree",
                                       "owner": "active automation worker", "kind": "local_assignment", "id": branch})
        # Branch and HEAD are durable, path-free provenance for this machine-local check.
        summary.append({"branch": branch, "head": item.get("head"), "changed_paths": sorted(changed),
                        "has_uncommitted_function_work": bool(dirty)})
    return claims, {"status": "complete", "checked_at": int(time.time()), "worktrees": summary}


def repository_slug(root: Path) -> str:
    remote = _run(["git", "remote", "get-url", "origin"], root).strip()
    match = re.search(r"github\.com[:/]([^/\s]+/[^/\s]+?)(?:\.git)?$", remote)
    if not match:
        raise ValueError("origin must identify a GitHub owner/repository; pass --repo explicitly")
    return match.group(1)


def synchronize_ownership(root: Path, repo: str) -> dict:
    """Read all configured ownership sources; fail closed if any source cannot be checked."""
    checked_at = int(time.time())
    functions = catalog.read_dict_csv(root / "symbols/functions.csv")
    import_rows = catalog.read_dict_csv(root / "symbols/imports.csv")
    names = {row["name"] for row in functions if row.get("name")}
    names.update(row["symbol"] for row in import_rows if row.get("symbol"))
    candidate_addresses = {catalog.address(row["address"])
                           for row in catalog.read_dict_csv(root / "symbols/ghidra_functions.csv")}
    claims: list[dict] = []
    source_records = []

    registry_claims = [{"address": catalog.address(row["address"]), "symbol": row["name"],
                        "source": "symbols/functions.csv", "owner": "Paleblood registry",
                        "kind": "implemented"}
                       for row in functions if row.get("status") in {"replaced", "edge-verified", "verified"}]
    claims.extend(registry_claims)
    source_records.append({"source": "replacement_registry", "status": "complete", "count": len(registry_claims),
                           "checked_at": checked_at, "fingerprint": catalog.sha256(root / "symbols/functions.csv")})

    ownership_path = root / "symbols/ownership.csv"
    if ownership_path.exists():
        metadata_rows = catalog.read_dict_csv(ownership_path)
        for row in metadata_rows:
            claims.append({**row, "source": "symbols/ownership.csv", "kind": "repository_claim"})
    source_records.append({"source": "repository_ownership_metadata", "status": "complete",
                           "count": len(catalog.read_dict_csv(ownership_path)) if ownership_path.exists() else 0,
                           "checked_at": checked_at,
                           "fingerprint": catalog.sha256(ownership_path) if ownership_path.exists() else "absent"})

    issues = json.loads(_run(["gh", "issue", "list", "--repo", repo, "--state", "open", "--limit", "500",
                              "--json", "number,title,body,url,author,assignees,updatedAt"], root))
    if len(issues) >= 500:
        raise ValueError("open issue inventory reached the API limit; paginate before claiming ownership complete")
    issue_claims = []
    issue_fingerprints = []
    for issue in issues:
        number = issue["number"]
        detail = json.loads(_run(["gh", "issue", "view", str(number), "--repo", repo,
                                  "--json", "number,title,body,url,author,assignees,comments"], root))
        if len(detail.get("comments", [])) >= 100:
            raise ValueError(f"issue #{number} comment inventory reached the API limit; ownership is incomplete")
        owner = ",".join(a["login"] for a in detail.get("assignees", [])) or "active issue claimant"
        source = f"issue #{number}"
        url = detail.get("url", f"https://github.com/{repo}/issues/{number}")
        body = detail.get("body", "")
        if detail.get("assignees"):
            issue_claims.extend(_extract_claims(body, source, owner, url, names, True, candidate_addresses))
        else:
            author = detail.get("author", {}).get("login", "issue author")
            claim_lines = "\n".join(line for line in body.splitlines()
                                     if re.search(r"\b(claim(?:ing|ed)?|taking|working on|implement(?:ing)?|assigned to)\b",
                                                  line, re.IGNORECASE))
            issue_claims.extend(_extract_claims(claim_lines, source, author, url, names, True,
                                                candidate_addresses))
        for comment in detail.get("comments", []):
            login = comment.get("author", {}).get("login", "unknown contributor")
            body = comment.get("body", "")
            extracted = _extract_claims(body, source, login, url, names, True, candidate_addresses)
            if re.search(r"\b(releas(?:e|ing|ed)|not taking|no longer claim|without source changes)\b",
                         body, re.IGNORECASE):
                released = {(claim.get("address"), claim.get("symbol")) for claim in extracted}
                issue_claims = [claim for claim in issue_claims
                                if ((claim.get("address"), claim.get("symbol")) not in released
                                    or claim.get("owner") not in {login, "active issue claimant"})]
            elif re.search(r"\b(tak(?:e|ing)|claim(?:ed)?|working on|ready to push|started work)\b",
                           body, re.IGNORECASE):
                issue_claims.extend(extracted)
        issue_fingerprints.append({"number": number, "updated_at": detail.get("updatedAt"),
                                   "author": detail.get("author", {}).get("login"),
                                   "body_sha256": hashlib.sha256(detail.get("body", "").encode()).hexdigest(),
                                   "comments_sha256": hashlib.sha256(catalog.canonical([
                                       {"author": c.get("author", {}).get("login"), "body": c.get("body", ""),
                                        "created_at": c.get("createdAt")}
                                       for c in detail.get("comments", [])]).encode()).hexdigest()})
    claims.extend(issue_claims)
    source_records.append({"source": "open_issues_and_comments", "status": "complete", "count": len(issues),
                           "checked_at": checked_at,
                           "fingerprint": hashlib.sha256(catalog.canonical(issue_fingerprints).encode()).hexdigest()})

    prs = json.loads(_run(["gh", "pr", "list", "--repo", repo, "--state", "open", "--limit", "500",
                           "--json", "number,title,body,url,author,headRefOid,files"], root))
    if len(prs) >= 500:
        raise ValueError("open PR inventory reached the API limit; paginate before claiming ownership complete")
    pr_claims = []
    for pr in prs:
        if len(pr.get("files", [])) >= 100:
            raise ValueError(f"PR #{pr['number']} file inventory reached the API limit; ownership is incomplete")
        number = pr["number"]
        author = pr.get("author", {}).get("login", "unknown contributor")
        url = pr.get("url", f"https://github.com/{repo}/pull/{number}")
        source = f"PR #{number}"
        pr_claims.extend(_extract_claims(pr.get("title", "") + "\n" + pr.get("body", ""), source, author, url, names, True,
                                        candidate_addresses))
        for changed in pr.get("files", []):
            filename = changed.get("path", "")
            for row in functions:
                if row.get("name") and row["name"] in filename:
                    pr_claims.append({"address": catalog.address(row["address"]), "symbol": row["name"],
                                      "source": source, "owner": author, "kind": "active_pr", "url": url})
    claims.extend(pr_claims)
    source_records.append({"source": "open_pull_requests", "status": "complete", "count": len(prs),
                           "checked_at": checked_at,
                           "fingerprint": hashlib.sha256(catalog.canonical([
                               {"number": pr.get("number"), "head": pr.get("headRefOid"),
                                "files": [item.get("path") for item in pr.get("files", [])],
                                "title": pr.get("title"), "author": pr.get("author", {}).get("login"),
                                "body_sha256": hashlib.sha256(pr.get("body", "").encode()).hexdigest()}
                               for pr in prs]).encode()).hexdigest()})

    local_claims, local_record = _local_worktree_evidence(root, names, candidate_addresses)
    claims.extend(local_claims)
    source_records.append({"source": "local_work_assignments", "status": local_record["status"],
                           "count": len(local_claims), "checked_at": local_record["checked_at"],
                           "fingerprint": hashlib.sha256(catalog.canonical(local_record["worktrees"]).encode()).hexdigest()})

    claims = sorted({_claim_key({**claim, "checked_at": claim.get("checked_at", checked_at)}):
                     _safe_claim({**claim, "checked_at": claim.get("checked_at", checked_at)})
                     for claim in claims}.values(),
                    key=lambda claim: (claim.get("address", ""), claim.get("symbol", ""), claim["source"]))
    stable_claims = [{key: value for key, value in claim.items() if key != "checked_at"} for claim in claims]
    snapshot_material = {"claims": stable_claims,
                        "sources": [{key: value for key, value in record.items()
                                     if key not in {"checked_at", "completed_at"}}
                                    for record in source_records]}
    return {"schema_version": 1, "snapshot_complete": all(r["status"] == "complete" for r in source_records),
            "checked_at": checked_at, "repository": repo, "sources": source_records,
            "local_worktrees": local_record["worktrees"], "claims": claims,
            "snapshot_sha256": hashlib.sha256(catalog.canonical(snapshot_material).encode()).hexdigest()}


def _claim_key(claim: dict) -> tuple:
    return (claim.get("address"), claim.get("symbol"), claim.get("system"), claim.get("source"),
            claim.get("owner"), claim.get("kind"))


def read_elf_executable_segments(path: Path, image_base: int = 0x400000) -> tuple[str, list[dict]]:
    """Read only ELF metadata and executable PT_LOAD extents; never persist file bytes."""
    digest = catalog.sha256(path)
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:4] != b"\x7fELF" or header[4] != 2 or header[5] != 1:
            raise ValueError("expected a 64-bit little-endian ELF")
        e_machine = struct.unpack_from("<H", header, 18)[0]
        if e_machine != 0x3E:
            raise ValueError(f"expected x86-64 ELF, found machine {e_machine}")
        phoff = struct.unpack_from("<Q", header, 32)[0]
        phentsize, phnum = struct.unpack_from("<HH", header, 54)
        if phentsize < 56 or phnum > 4096:
            raise ValueError("ELF program-header table is malformed")
        stream.seek(phoff)
        segments = []
        for _ in range(phnum):
            raw = stream.read(phentsize)
            if len(raw) != phentsize:
                raise ValueError("truncated ELF program-header table")
            p_type, p_flags = struct.unpack_from("<II", raw, 0)
            p_offset, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from("<QQQQQQ", raw, 8)
            if p_type == 1 and p_flags & 1:
                segments.append({"start": p_vaddr + image_base, "end": p_vaddr + image_base + p_memsz,
                                 "file_end": p_vaddr + image_base + p_filesz,
                                 "offset": p_offset, "file_size": p_filesz, "flags": p_flags})
    return digest, segments


def _decode_with_objdump(code: bytes, start: int) -> list[dict]:
    if not hasattr(os, "memfd_create"):
        raise RuntimeError("anonymous memfd is required to decode without writing game bytes")
    fd = os.memfd_create("pb-function-decode", flags=0)
    try:
        with os.fdopen(os.dup(fd), "wb") as stream:
            stream.write(code)
        result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
                                 f"--adjust-vma=0x{start:x}", f"/proc/self/fd/{fd}"],
                                pass_fds=(fd,), check=True, capture_output=True, text=True)
    finally:
        os.close(fd)
    instructions = []
    for line in result.stdout.splitlines():
        match = INSTRUCTION.match(line)
        if not match:
            continue
        raw_bytes = bytes.fromhex(match.group(2))
        mnemonic = (match.group(3) or "").lower()
        operands = match.group(4).strip().lower()
        explicit_memory = "[" in operands
        stack_memory = mnemonic.split(".")[0] in IMPLICIT_STACK_MEMORY or "[rsp" in operands or "[rbp" in operands
        implicit_pointer = mnemonic.split(".")[0] in IMPLICIT_POINTER_MEMORY
        instructions.append({"address": int(match.group(1), 16), "size": len(raw_bytes),
                             "bytes": raw_bytes.hex(), "mnemonic": mnemonic, "operands": operands,
                             "is_memory": explicit_memory or stack_memory or implicit_pointer,
                             "is_stack_memory": stack_memory, "is_call": mnemonic.startswith("call"),
                             "is_indirect_call": mnemonic.startswith("call") and "[" in operands,
                             "is_branch": mnemonic.startswith(("j", "loop")),
                             "is_conditional_branch": mnemonic.startswith("j") and mnemonic not in {"jmp", "jmpq"}})
    return instructions


def boundary_and_dependency_evidence(row: dict, edges: list[dict], elf_path: Path,
                                     segments: list[dict], overlaps: set[str] | None = None) -> dict:
    start, size = int(row["address"], 16), row.get("size_bytes")
    result = {"boundary_class": "ghidra_only", "function_kind": "ordinary_candidate", "evidence": [], "blockers": [],
              "dependencies": {"status": "unknown", "direct_callees": [], "imports": [],
                               "indirect_calls": [], "unresolved_calls": [], "memory": "unknown",
                               "memory_accesses": [], "static_references": None, "potential_side_effects": []}}
    if size is None or size <= 0:
        result["blockers"].append("function extent is missing")
        return result
    segment = next((item for item in segments if item["start"] <= start and start + size <= item["file_end"]), None)
    if not segment:
        result["boundary_class"] = "non_executable_or_out_of_file"
        result["blockers"].append("Ghidra extent is not contained in file-backed executable PT_LOAD data")
        return result
    if row["address"] in (overlaps or set()):
        result["function_kind"] = "potentially_overlapping"
        result["boundary_class"] = "overlapping_extent"
        result["blockers"].append("Ghidra extent overlaps another catalog function")
        return result
    result["evidence"].append({"type": "elf_executable_extent", "source": "ELF PT_LOAD flags/range",
                              "confidence": "high"})
    offset = segment["offset"] + (start - segment["start"])
    with elf_path.open("rb") as stream:
        stream.seek(offset)
        code = stream.read(size)
    if len(code) != size:
        result["blockers"].append("function extent is truncated in the executable")
        return result
    try:
        decoded = _decode_with_objdump(code, start)
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        result["blockers"].append(f"independent instruction decode failed: {error}")
        return result
    decoded_bytes = sum(insn["size"] for insn in decoded)
    if not decoded or decoded_bytes != size or any(not i["mnemonic"] for i in decoded):
        result["boundary_class"] = "decoder_uncertain"
        result["blockers"].append("independent decoder did not cover the complete Ghidra extent")
        return result
    if row.get("instruction_count") is not None and len(decoded) != row["instruction_count"]:
        result["blockers"].append("independent instruction count differs from Ghidra listing")
        return result
    if decoded[-1]["mnemonic"] not in TERMINATORS:
        result["blockers"].append("extent does not end at a recognized control-flow terminator")
        return result
    substantive = [insn for insn in decoded if insn["mnemonic"].split(".")[0]
                   not in {"ret", "retq", "retf", "nop", "nopl", "nopw", "endbr64", "int3"}]
    if not substantive:
        result["function_kind"] = "trivial_return_or_padding"
        result["boundary_class"] = "trivial_body"
        result["blockers"].append("body contains only return/padding instructions, with no behavior to compare")
        return result
    if len(decoded) <= 2 and decoded[-1]["mnemonic"] in {"jmp", "jmpq"}:
        result["function_kind"] = "import_thunk_or_tail_stub" if "[" in decoded[-1]["operands"] else "tail_thunk"
        result["boundary_class"] = "thunk_or_stub"
        result["blockers"].append("single-branch thunk/stub requires target classification before scheduling")
        return result
    incoming = [edge for edge in edges if edge["callee"] == row["address"] and edge["kind"] == "direct"]
    if not incoming:
        result["blockers"].append("no direct-call cross-reference supports this candidate entry")
        return result
    result["evidence"].extend([
        {"type": "independent_instruction_decode", "source": "GNU objdump x86-64 decoder",
         "confidence": "high", "instruction_count": len(decoded), "bytes_covered": decoded_bytes},
        {"type": "direct_call_target", "source": "Ghidra call-edge export", "confidence": "medium",
         "count": len(incoming)}])
    result["boundary_class"] = "cross_tool_supported"
    deps = row.get("calls", {})
    direct = [entry["target"] for entry in deps.get("direct", [])]
    imports = [entry["target"] for entry in deps.get("imports", [])]
    indirect = [entry["target"] for entry in deps.get("indirect", [])]
    unresolved = [entry["target"] for key in ("unresolved_direct", "unresolved_indirect")
                  for entry in deps.get(key, [])]
    start, end = int(row["address"], 16), int(row["address"], 16) + size
    tail_jumps = []
    indirect_jumps = []
    for insn in decoded:
        if insn["mnemonic"] not in {"jmp", "jmpq"}:
            continue
        target = re.match(r"0x([0-9a-f]+)\b", insn["operands"])
        if target:
            target_address = int(target.group(1), 16)
            if not start <= target_address < end:
                tail_jumps.append(f"0x{target_address:08x}")
        else:
            indirect_jumps.append(insn["address"])
    direct.extend(target for target in tail_jumps if target not in direct)
    unresolved.extend(f"indirect_jump@0x{location:08x}" for location in indirect_jumps)
    static_references = row.get("memory_dependencies", {}).get("static_references")
    accesses = []
    for insn in decoded:
        if not insn["is_memory"]:
            continue
        operands = insn["operands"].split(",")
        destination_is_memory = "[" in operands[0] if operands else False
        mnemonic = insn["mnemonic"].split(".")[0]
        if mnemonic == "lea":
            access = "address_calculation"
        elif mnemonic in {"cmp", "test", "push"} or not destination_is_memory:
            access = "read"
        elif mnemonic in {"mov", "stos", "xchg", "pop"}:
            access = "write"
        else:
            access = "read_write_or_unknown"
        operand_text = insn["operands"]
        region = "stack" if insn["is_stack_memory"] else (
            "rip_relative" if "[rip" in operand_text else "register_indirect")
        accesses.append({"instruction": insn["address"], "mnemonic": mnemonic,
                         "access": access, "region": region})
    result["dependencies"].update({"status": "static_call_scan_complete" if deps.get("analysis_complete")
                                   else "unknown", "direct_callees": direct, "imports": imports,
                                   "indirect_calls": indirect, "unresolved_calls": unresolved,
                                   "memory": "unknown", "memory_accesses": accesses,
                                   "static_references": static_references,
                                   "potential_side_effects": (["memory write or read/write access"]
                                                               if any(item["access"] in {"write", "read_write_or_unknown"}
                                                                      for item in accesses) else [])})
    stack_only = all(not insn["is_memory"] or insn["mnemonic"].split(".")[0] in {"ret", "retq", "retf"}
                     for insn in decoded)
    no_calls = not (direct or imports or indirect or unresolved) and not any(i["is_call"] for i in decoded)
    branch_targets = []
    for insn in decoded:
        if insn["is_branch"]:
            target = re.match(r"0x([0-9a-f]+)\b", insn["operands"])
            branch_targets.append(int(target.group(1), 16) if target else None)
    bounded_branches = all(target is not None and target > insn["address"]
                           for insn, target in zip((i for i in decoded if i["is_branch"]), branch_targets))
    bounded_branches = bounded_branches and not any(i["is_indirect_call"] for i in decoded)
    safe_instructions = not any(i["mnemonic"].split(".")[0] in FORBIDDEN for i in decoded)
    if no_calls and stack_only and deps.get("analysis_complete"):
        result["dependencies"]["memory"] = "stack_only_static_accesses" if any(i["is_memory"] for i in decoded) else "no_memory_operands_seen"
        result["eligible_for_fixture_generation"] = True
    else:
        result["eligible_for_fixture_generation"] = False
        result["blockers"].append("incomplete calls, calls, or data-memory/stack-frame accesses require dependency fixtures")
    result["eligible_for_original_execution"] = bool(result.get("eligible_for_fixture_generation")
                                                       and bounded_branches and safe_instructions)
    if not result["eligible_for_original_execution"]:
        result["blockers"].append("original-instruction runner safety requirements are not established")
    result["eligible_for_replacement_development"] = bool(result.get("eligible_for_original_execution")
                                                           and not any(i["is_memory"] for i in decoded))
    if not result["eligible_for_replacement_development"]:
        result["blockers"].append("ABI signature or bounded memory semantics are not established")
    else:
        # Original-only fixtures do not establish the prototype needed for a native replacement.
        result["eligible_for_replacement_development"] = False
        result["blockers"].append("replacement development requires an independently checked ABI signature")
    return result


def classify_ownership(address: str, name: str | None, snapshot: dict,
                       system: str | None = None) -> dict:
    matches = [claim for claim in snapshot.get("claims", [])
               if claim.get("address") == address
               or (name and claim.get("symbol") == name)
               or (system and claim.get("system") == system)]
    if not ownership_snapshot_is_current(snapshot):
        return {"status": "unknown", "claims": matches, "evidence": snapshot.get("sources", []),
                "snapshot_sha256": snapshot.get("snapshot_sha256"),
                "blockers": ["ownership sources are missing, incomplete, stale or future-dated; refresh without releasing claims"]}
    implemented = [claim for claim in matches if claim.get("kind") == "implemented"]
    contributor = [claim for claim in matches if claim.get("kind") in {"contributor", "active_pr", "repository_claim"}]
    automation = [claim for claim in matches if claim.get("kind") == "local_assignment"]
    if implemented:
        status = "already_implemented"
    elif (contributor and automation) or len({c.get("owner") for c in contributor}) > 1:
        status = "ownership_conflict"
    elif contributor:
        status = "contributor_owned"
    elif automation:
        status = "active_automation_worker"
    else:
        status = "confirmed_unclaimed"
    evidence = snapshot.get("sources", [])
    return {"status": status, "claims": matches, "snapshot_sha256": snapshot.get("snapshot_sha256"),
            "evidence": evidence,
            "blockers": ["conflicting ownership evidence requires human resolution"]
            if status == "ownership_conflict" else []}


def ownership_snapshot_is_current(snapshot: dict, now: int | None = None) -> bool:
    """Require complete, timestamped ownership evidence refreshed within one day."""
    if not snapshot.get("snapshot_complete") or not isinstance(snapshot.get("checked_at"), int):
        return False
    sources = snapshot.get("sources", [])
    names = [source.get("source") for source in sources]
    if not REQUIRED_OWNERSHIP_SOURCES.issubset(set(names)) or len(names) != len(set(names)):
        return False
    if not sources or any(source.get("status") != "complete"
                          or not isinstance(source.get("checked_at"), int) for source in sources):
        return False
    current = int(time.time()) if now is None else now
    return all(0 <= current - stamp <= OWNERSHIP_MAX_AGE_SECONDS
               for stamp in [snapshot["checked_at"], *(source["checked_at"] for source in sources)])


def qualification_key(row: dict, metadata: dict, ownership: dict, executable_sha: str) -> str:
    stable_ownership = {key: value for key, value in ownership.items() if key not in {"evidence"}}
    stable_ownership["claims"] = [{key: value for key, value in claim.items() if key != "checked_at"}
                                   for claim in ownership.get("claims", [])]
    material = {"catalog_key": row["cache_key"], "catalog_identity": metadata.get("catalog_identity"),
                "executable_sha256": executable_sha, "qualification_version": QUALIFICATION_VERSION,
                "qualification_tools": metadata.get("qualification_tools"),
                "ownership": stable_ownership, "address": row["address"]}
    return hashlib.sha256(catalog.canonical(material).encode()).hexdigest()


def qualification_tool_identity(root: Path) -> dict:
    decoder_version = _run(["objdump", "--version"]).splitlines()[0].strip()
    return {"qualification_code_sha256": catalog.sha256(Path(__file__)),
            "catalog_tool_sha256": catalog.sha256(Path(catalog.__file__)),
            "objdump_version": decoder_version,
            "harness_sha256": catalog.sha256(root / "tools/harness.py"),
            "loader_source_sha256": catalog.sha256(root / "runtime/loader.c")}


def qualify_rows(functions: list[dict], edges: list[dict], snapshot: dict, executable_sha: str,
                 elf_path: Path, segments: list[dict], metadata: dict | None = None,
                 batch_size: int = 20, previous: dict[str, dict] | None = None) -> list[dict]:
    if batch_size < 1:
        raise ValueError("batch size must be positive")
    candidates = []
    for row in functions:
        owner = classify_ownership(row["address"], row.get("name"), snapshot, row.get("system"))
        if owner["status"] in {"already_implemented", "contributor_owned", "active_automation_worker"}:
            continue
        if row["replacement_status"] in {"replaced", "edge-verified", "verified"}:
            continue
        if row.get("instruction_count") is None or row["instruction_count"] <= 1:
            continue
        candidates.append((row, owner))
    # Prefer strong static evidence and short, fully scanned leaves; this is only ordering.
    candidates.sort(key=lambda item: (not any(call.get("kind") == "direct"
                                                for call in item[0]["calls"].get("callers", [])),
                                      not item[0]["calls"]["analysis_complete"],
                                      len(item[0]["calls"]["direct"]) + len(item[0]["calls"]["indirect"])
                                      + len(item[0]["calls"]["imports"])
                                      + len(item[0]["calls"]["unresolved_direct"])
                                      + len(item[0]["calls"]["unresolved_indirect"]),
                                      bool(item[0]["memory_dependencies"].get("static_references")),
                                      bool(item[0].get("classification", {}).get("conflict")),
                                      not bool(item[0].get("type_intelligence", {}).get("names")),
                                      item[0].get("complexity", {}).get("value") or 1 << 30,
                                      item[0].get("size_bytes") or 1 << 30,
                                      item[0]["address"]))
    overlapping: set[str] = set()
    active: list[tuple[int, str]] = []
    for row in sorted(functions, key=lambda item: (int(item["address"], 16), item.get("size_bytes") or 0)):
        start, end = int(row["address"], 16), int(row["address"], 16) + (row.get("size_bytes") or 0)
        active = [(other_end, other_addr) for other_end, other_addr in active if other_end > start]
        if active:
            overlapping.add(row["address"])
            overlapping.update(other_addr for _, other_addr in active)
        active.append((end, row["address"]))
    results = []
    for row, owner in candidates[:batch_size]:
        key = qualification_key(row, metadata or {}, owner, executable_sha)
        cached = (previous or {}).get(row["address"])
        if cached and cached.get("qualification_cache_key") == key:
            results.append(cached)
            continue
        evidence = boundary_and_dependency_evidence(row, edges, elf_path, segments, overlapping)
        if owner["status"] != "confirmed_unclaimed":
            for stage in ("eligible_for_fixture_generation", "eligible_for_original_execution",
                          "eligible_for_replacement_development"):
                evidence[stage] = False
            evidence["blockers"].append("ownership is not confirmed unclaimed")
        if evidence["boundary_class"] != "cross_tool_supported":
            for stage in ("eligible_for_fixture_generation", "eligible_for_original_execution",
                          "eligible_for_replacement_development"):
                evidence[stage] = False
        blockers = list(evidence["blockers"])
        if evidence.get("eligible_for_replacement_development"):
            classification = "eligible_for_replacement_development"
        elif evidence.get("eligible_for_original_execution"):
            classification = "eligible_for_original_instruction_execution"
        elif evidence.get("eligible_for_fixture_generation"):
            classification = "eligible_for_fixture_generation"
        elif owner["status"] == "contributor_owned":
            classification = "contributor_owned"
        elif owner["status"] == "already_implemented":
            classification = "already_implemented"
        elif owner["status"] == "unknown":
            classification = "conditionally_eligible"
        else:
            classification = "blocked"
        results.append({"address": row["address"], "name": row["name"], "executable_sha256": executable_sha,
                        "size_bytes": row.get("size_bytes"),
                        "ghidra_instruction_count": row.get("instruction_count"),
                        "ghidra_complexity_estimate": row.get("complexity"),
                        "capture_provenance": row.get("capture_provenance"),
                        "fixture_requirements": {"register_state": "deterministic System V AMD64 register values",
                                                  "stack": "isolated harness stack",
                                                  "memory": evidence["dependencies"]["memory"],
                                                  "formal_argument_signature": "unknown"},
                        "expected_verification_difficulty": {"original_execution": "low; leaf code with bounded control flow",
                                                              "replacement": "unknown; ABI signature and behavior remain unestablished"},
                        "ownership": owner, **evidence, "classification": classification,
                        "qualification_version": QUALIFICATION_VERSION,
                        "catalog_identity": (metadata or {}).get("catalog_identity"),
                        "ownership_snapshot_sha256": snapshot.get("snapshot_sha256"),
                        "qualification_tools": (metadata or {}).get("qualification_tools"),
                        "confidence": {"boundary": evidence["boundary_class"],
                                       "ownership": owner["status"],
                                       "dependency": evidence["dependencies"]["status"]},
                        "blockers": sorted(set(blockers)), "qualification_cache_key": key,
                        "analyzed_at": int(time.time())})
    return results


def save_qualifications(state: Path, results: list[dict], fail_after: int | None = None) -> None:
    db = catalog.connect(state)
    try:
        db.execute("""CREATE TABLE IF NOT EXISTS qualifications (
          address TEXT PRIMARY KEY, cache_key TEXT NOT NULL, data TEXT NOT NULL)""")
        db.execute("BEGIN IMMEDIATE")
        for index, result in enumerate(results, 1):
            db.execute("INSERT INTO qualifications VALUES (?, ?, ?) ON CONFLICT(address) DO UPDATE SET "
                       "cache_key=excluded.cache_key,data=excluded.data",
                       (result["address"], result["qualification_cache_key"], catalog.canonical(result)))
            if fail_after is not None and index == fail_after:
                raise RuntimeError("injected interrupted qualification write")
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


def load_qualifications(state: Path, expected_tools: dict | None = None) -> list[dict]:
    db = catalog.connect(state)
    try:
        exists = db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='qualifications'").fetchone()
        if not exists:
            return []
        catalog_row = db.execute("SELECT value FROM metadata WHERE key='catalog_identity'").fetchone()
        owner_exists = db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='ownership_snapshots'").fetchone()
        owner_row = db.execute("SELECT snapshot_sha256,data FROM ownership_snapshots WHERE id=1").fetchone() if owner_exists else None
        catalog_identity = json.loads(catalog_row[0]) if catalog_row else None
        owner_sha = owner_row[0] if owner_row else None
        owner_snapshot = json.loads(owner_row[1]) if owner_row else {}
        if expected_tools is not None and not ownership_snapshot_is_current(owner_snapshot):
            return []
        results = [item for (raw,) in db.execute("SELECT data FROM qualifications ORDER BY address")
                   if (item := json.loads(raw)).get("qualification_version") == QUALIFICATION_VERSION
                   and item.get("catalog_identity") == catalog_identity
                   and item.get("ownership_snapshot_sha256") == owner_sha
                   and (expected_tools is None or item.get("qualification_tools") == expected_tools)]
        if expected_tools is not None:
            for item in results:
                item["ownership"]["evidence"] = owner_snapshot.get("sources", [])
                item["ownership_revalidated_at"] = owner_snapshot.get("checked_at")
        return results
    finally:
        db.close()


def cache_snapshot(state: Path, snapshot: dict) -> None:
    db = catalog.connect(state)
    try:
        db.execute("""CREATE TABLE IF NOT EXISTS ownership_snapshots (
          id INTEGER PRIMARY KEY CHECK(id=1), snapshot_sha256 TEXT NOT NULL, data TEXT NOT NULL)""")
        db.execute("""CREATE TABLE IF NOT EXISTS qualifications (
          address TEXT PRIMARY KEY, cache_key TEXT NOT NULL, data TEXT NOT NULL)""")
        db.execute("BEGIN IMMEDIATE")
        old = db.execute("SELECT snapshot_sha256 FROM ownership_snapshots WHERE id=1").fetchone()
        if old and old[0] != snapshot["snapshot_sha256"]:
            db.execute("DELETE FROM qualifications")
            if db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='candidate_inventories'").fetchone():
                db.execute("DELETE FROM candidate_inventories")
        db.execute("INSERT INTO ownership_snapshots VALUES (1, ?, ?) ON CONFLICT(id) DO UPDATE SET "
                   "snapshot_sha256=excluded.snapshot_sha256,data=excluded.data",
                   (snapshot["snapshot_sha256"], catalog.canonical(snapshot)))
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


def load_ownership(state: Path) -> dict:
    db = catalog.connect(state)
    try:
        exists = db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='ownership_snapshots'").fetchone()
        if not exists:
            return {"snapshot_complete": False, "claims": [], "sources": []}
        row = db.execute("SELECT data FROM ownership_snapshots WHERE id=1").fetchone()
        return json.loads(row[0]) if row else {"snapshot_complete": False, "claims": [], "sources": []}
    finally:
        db.close()
