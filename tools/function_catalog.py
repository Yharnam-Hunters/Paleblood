#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Evidence-aware function inventory, call graph, and local scheduling catalog.

The checked-in Ghidra export supplies entry addresses and byte sizes. Optional analysis CSVs
add instruction counts, call edges, and dependency completeness. Missing fields stay unknown.
The SQLite database is local state; use `export` for a portable JSON snapshot.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import sqlite3
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

from verification import source_hash as replacement_source_hash

SCHEMA_VERSION = 1
TOOL_VERSION = "1.1.1"
EDGE_FIELDS = ("caller", "callee", "kind", "reference", "source", "confidence")
METRIC_FIELDS = ("address", "instruction_count", "complexity", "fixture_difficulty",
                 "data_references", "boundary_confidence", "analysis_complete", "source")
RELEVANCE_FIELDS = ("address", "score", "source")
DEFAULT_RANK_WEIGHTS = {"feasibility": 20, "dependency": 15, "fixture_difficulty": 10,
                        "analysis_confidence": 10, "unlock": 15, "runtime_relevance": 15,
                        "ownership": 5, "original_execution": 15, "engineering_effort": 5,
                        "test_infrastructure": 5, "type_context": 10}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def canonical(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def read_dict_csv(path: Path, required: tuple[str, ...] | None = None) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None:
            raise ValueError(f"{path}: missing CSV header")
        if required and tuple(reader.fieldnames) != required:
            raise ValueError(f"{path}: expected header {','.join(required)}")
        return [dict(row) for row in reader]


def address(value: str) -> str:
    number = int(value, 0)
    if number < 0 or number > 0xFFFFFFFFFFFFFFFF:
        raise ValueError(f"invalid address: {value}")
    return f"0x{number:08x}"


def _optional_int(value: str | None) -> int | None:
    if value in (None, "", "unknown", "null"):
        return None
    result = int(value, 0)
    if result < 0:
        raise ValueError(f"expected nonnegative integer, got {value}")
    return result


def load_claims(path: Path | None, complete: bool) -> tuple[list[dict], str | None, bool]:
    if path is None:
        return [], None, False
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("claims JSON must be an object")
    claims = raw.get("claims")
    if not isinstance(claims, list):
        raise ValueError("claims JSON must contain a claims array")
    normalized = []
    for claim in claims:
        if not isinstance(claim, dict):
            raise ValueError("each ownership claim must be an object")
        if "address" in claim:
            normalized.append({**claim, "address": address(claim["address"])})
        elif "system" in claim:
            normalized.append(claim)
        else:
            raise ValueError("each claim needs an address or system")
    return normalized, sha256(path), bool(raw.get("snapshot_complete", complete))


def load_capture_provenance(path: Path | None) -> tuple[dict[str, list[dict]], str | None]:
    if path is None:
        return {}, None
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("capture provenance JSON must be an object")
    functions = raw.get("functions")
    if not isinstance(functions, list):
        raise ValueError("capture provenance JSON must contain a functions array")
    result: dict[str, list[dict]] = {}
    allowed = {"evidence_type", "capture_set", "case_id", "recorded_at", "source_sha256", "reference"}
    for item in functions:
        if not isinstance(item, dict) or "address" not in item:
            raise ValueError("each capture provenance function needs an address")
        key = address(item["address"])
        captures = item.get("captures")
        if not isinstance(captures, list):
            raise ValueError(f"capture provenance for {key} needs a captures array")
        for capture in captures:
            if not isinstance(capture, dict):
                raise ValueError(f"capture provenance for {key} must contain objects")
            if set(capture) - allowed:
                raise ValueError(f"capture metadata for {key} contains unsupported fields (paths/content are forbidden)")
            if capture.get("evidence_type") not in ("gameplay", "synthetic", "other"):
                raise ValueError(f"capture provenance for {key} needs evidence_type gameplay, synthetic, or other")
            digest = capture.get("source_sha256")
            if digest is not None and (len(digest) != 64 or any(c not in "0123456789abcdefABCDEF" for c in digest)):
                raise ValueError(f"capture source hash for {key} must be a SHA-256 hex digest")
            reference = capture.get("reference", "")
            if reference.startswith("/") or "/home/" in reference or "/mnt/" in reference:
                raise ValueError(f"capture reference for {key} must not contain a machine-specific path")
        result[key] = captures
    return result, sha256(path)


def _claim_state(row: dict, claims: list[dict], snapshot_complete: bool) -> dict:
    matches = []
    for claim in claims:
        if claim.get("address") == row["address"] or (
            claim.get("system") and row.get("system") == claim["system"]
        ):
            safe = {k: claim[k] for k in ("source", "owner", "kind", "id") if k in claim}
            url = claim.get("url", "")
            if url.startswith("https://github.com/"):
                safe["url"] = url
            matches.append(safe)
    if matches:
        return {"status": "claimed", "claims": matches}
    if snapshot_complete:
        return {"status": "no_recorded_claim", "claims": []}
    return {"status": "unknown", "claims": []}


def build_rows(root: Path, executable_sha256: str | None = None,
               edges_path: Path | None = None, metrics_path: Path | None = None,
               claims_path: Path | None = None, claims_complete: bool = False,
               relevance_path: Path | None = None,
               captures_path: Path | None = None,
               analysis_version: str = "unknown",
               config: dict | None = None) -> tuple[list[dict], list[dict], dict]:
    symbols = root / "symbols"
    functions_path = symbols / "ghidra_functions.csv"
    named_path = symbols / "functions.csv"
    verification_path = symbols / "verification.csv"
    for path in (functions_path, named_path):
        if not path.is_file():
            raise ValueError(f"required analysis input missing: {path}")

    artifact_paths = {"ghidra_functions": functions_path, "functions": named_path}
    if verification_path.exists():
        artifact_paths["verification"] = verification_path
    if edges_path:
        artifact_paths["edges"] = edges_path
    if metrics_path:
        artifact_paths["metrics"] = metrics_path
    if relevance_path:
        artifact_paths["relevance"] = relevance_path
    if captures_path:
        artifact_paths["captures"] = captures_path
    artifact_hashes = {name: sha256(path) for name, path in artifact_paths.items()}

    named_rows = read_dict_csv(named_path)
    named_by_addr = {address(row["address"]): row for row in named_rows}
    verification: dict[str, list[dict]] = defaultdict(list)
    if verification_path.exists():
        for item in read_dict_csv(verification_path):
            verification[item["function"]].append(item)
    metric_by_addr = {}
    if metrics_path:
        for item in read_dict_csv(metrics_path, METRIC_FIELDS):
            key = address(item["address"])
            if key in metric_by_addr:
                raise ValueError(f"duplicate metrics row for {key}")
            metric_by_addr[key] = item
    relevance_by_addr = {}
    if relevance_path:
        for item in read_dict_csv(relevance_path, RELEVANCE_FIELDS):
            key = address(item["address"])
            score = int(item["score"])
            if score < 0 or score > 100:
                raise ValueError(f"runtime relevance score for {key} must be 0..100")
            if key in relevance_by_addr:
                raise ValueError(f"duplicate relevance row for {key}")
            relevance_by_addr[key] = {"score": score, "source": item["source"]}

    claim_rows, claim_hash, claim_snapshot_complete = load_claims(claims_path, claims_complete)
    if claim_hash:
        artifact_hashes["claims"] = claim_hash
    captures, captures_hash = load_capture_provenance(captures_path)
    if captures_hash:
        artifact_hashes["captures"] = captures_hash

    function_rows = []
    for item in read_dict_csv(functions_path, ("address", "size")):
        key = address(item["address"])
        size = _optional_int(item["size"])
        named = named_by_addr.get(key)
        metric = metric_by_addr.get(key)
        verify_rows = verification.get(named["name"], []) if named else []
        verification_summary = None
        if verify_rows:
            current_source = replacement_source_hash(str(root), named["name"]) if named else ""
            verification_summary = {
                "records": [{"cases": v["cases"], "option": v["option"], "total": int(v["total"]),
                             "passed": int(v["passed"]), "failed": int(v["failed"]),
                             "quarantined": int(v["quarantined"]), "source": v["source"],
                             "date": v["date"], "current": bool(current_source and v["source"] == current_source)}
                            for v in verify_rows],
                "status": named.get("status") if named else None,
                "current_source": current_source or None,
            }
        if metric:
            deps_complete = (edges_path is not None
                             and metric["analysis_complete"].lower() in ("true", "1", "yes"))
            metric_source = metric["source"] or "unspecified-analysis-artifact"
            boundary_confidence = metric["boundary_confidence"] or "unknown"
            instruction_count = _optional_int(metric["instruction_count"])
            complexity = _optional_int(metric["complexity"])
            fixture_difficulty = metric["fixture_difficulty"] or "unknown"
            data_references = (json.loads(metric["data_references"])
                               if metric["data_references"] else None)
            if data_references is not None and not isinstance(data_references, list):
                raise ValueError(f"data references for {key} must be a JSON array")
        else:
            deps_complete = False
            metric_source = None
            boundary_confidence = "ghidra-export-entry-size" if size is not None else "unknown"
            instruction_count = complexity = data_references = None
            fixture_difficulty = "unknown"
        if fixture_difficulty not in ("low", "medium", "high", "unknown"):
            raise ValueError(f"invalid fixture difficulty for {key}: {fixture_difficulty}")
        row = {
            "address": key,
            "size_bytes": size,
            "boundary": {"status": "ghidra-defined" if size is not None else "unknown",
                         "confidence": boundary_confidence,
                         "evidence": metric_source or "symbols/ghidra_functions.csv"},
            "instruction_count": instruction_count,
            "calls": {"analysis_complete": deps_complete, "callers": [], "direct": [], "indirect": [],
                      "unresolved_direct": [], "unresolved_indirect": [], "imports": [],
                      "evidence": metric_source},
            "memory_dependencies": {"static_references": data_references,
                                    "static_scan_complete": deps_complete,
                                    "indirect_accesses": None,
                                    "complete": False},
            "global_state_dependencies": None,
            "complexity": {"value": complexity, "method": "Ghidra conditional-flow count + 1"
                           if complexity is not None else None},
            "fixture_difficulty": fixture_difficulty,
            "runtime_relevance": relevance_by_addr.get(key),
            "name": named.get("name") if named else None,
            "system": named.get("system") if named else None,
            "replacement_status": named.get("status", "untracked") if named else "untracked",
            "verification": verification_summary,
            "capture_provenance": captures.get(key),
            "ownership": _claim_state({"address": key, "system": named.get("system") if named else None},
                                      claim_rows, claim_snapshot_complete),
            "blockers": None,
            "required_prerequisites": None,
        }
        function_rows.append(row)

    edge_rows = []
    if edges_path:
        for edge in read_dict_csv(edges_path, EDGE_FIELDS):
            caller = address(edge["caller"])
            if not edge["source"] or not edge["confidence"]:
                raise ValueError(f"call edge at {caller} needs source and confidence evidence")
            if edge["kind"] in ("direct", "indirect"):
                callee = address(edge["callee"]) if edge["callee"] else None
                if callee is None:
                    raise ValueError(f"resolved {edge['kind']} edge has no callee")
            elif edge["kind"] == "import":
                callee = edge["callee"] or None
                if callee is None:
                    raise ValueError("import edge needs an import identity in callee")
            else:
                callee = None
            if edge["kind"] not in ("direct", "indirect", "import", "unresolved_direct", "unresolved_indirect"):
                raise ValueError(f"unknown edge kind {edge['kind']!r}")
            edge_rows.append({"caller": caller, "callee": callee, "kind": edge["kind"],
                              "reference": address(edge["reference"]) if edge["reference"] else None,
                              "source": edge["source"], "confidence": edge["confidence"]})
    function_map = {row["address"]: row for row in function_rows}
    for edge in edge_rows:
        caller = function_map.get(edge["caller"])
        if caller is None:
            continue
        key = {"direct": "direct", "indirect": "indirect", "unresolved_direct": "unresolved_direct",
               "unresolved_indirect": "unresolved_indirect",
               "import": "imports"}[edge["kind"]]
        caller["calls"][key].append({"target": edge["callee"], "reference": edge["reference"],
                                    "source": edge["source"], "confidence": edge["confidence"]})
        if edge["callee"] in function_map:
            function_map[edge["callee"]]["calls"]["callers"].append(
                {"target": edge["caller"], "reference": edge["reference"],
                 "source": edge["source"], "confidence": edge["confidence"], "kind": edge["kind"]})

    if config is not None and not isinstance(config, dict):
        raise ValueError("analysis configuration must be a JSON object")
    config = {"analysis_version": analysis_version, **(config or {})}
    config_digest = hashlib.sha256(canonical(config).encode()).hexdigest()
    cache_inputs = {"schema": SCHEMA_VERSION, "tool": TOOL_VERSION, "executable_sha256": executable_sha256,
                    "config_sha256": config_digest, "analysis_version": analysis_version,
                    "artifacts": artifact_hashes}
    identity = hashlib.sha256(canonical(cache_inputs).encode()).hexdigest()
    for row in function_rows:
        row["cache_key"] = hashlib.sha256(
            f"{identity}:{row['address']}".encode()).hexdigest()
    metadata = {**cache_inputs, "catalog_identity": identity,
                "discovered": len(function_rows),
                "boundary_sized": sum(row["size_bytes"] is not None for row in function_rows),
                "independently_confirmed_boundaries": 0,
                "instruction_counts": sum(row["instruction_count"] is not None for row in function_rows),
                "functions_with_static_call_scan": sum(row["calls"]["analysis_complete"] for row in function_rows),
                "functions_with_unknown_call_graph": sum(not row["calls"]["analysis_complete"] for row in function_rows),
                "functions_with_capture_provenance": sum(row["capture_provenance"] is not None
                                                          for row in function_rows),
                "edges": len(edge_rows)}
    return function_rows, edge_rows, metadata


def default_state_path() -> Path:
    base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return base / "paleblood" / "function-catalog.sqlite3"


def connect(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path)
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("PRAGMA foreign_keys=ON")
    db.executescript("""
      CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS functions (address TEXT PRIMARY KEY, cache_key TEXT NOT NULL, data TEXT NOT NULL);
      CREATE INDEX IF NOT EXISTS functions_cache_key ON functions(cache_key);
      CREATE TABLE IF NOT EXISTS edges (caller TEXT NOT NULL, callee TEXT, kind TEXT NOT NULL,
          reference TEXT, source TEXT NOT NULL, confidence TEXT NOT NULL);
      CREATE INDEX IF NOT EXISTS edges_caller ON edges(caller);
      CREATE INDEX IF NOT EXISTS edges_callee ON edges(callee);
      CREATE TABLE IF NOT EXISTS intelligence_snapshot (id INTEGER PRIMARY KEY CHECK(id=1), data TEXT NOT NULL);
    """)
    return db


def write_snapshot(path: Path, functions: list[dict], edges: list[dict], metadata: dict,
                   fail_after: int | None = None, intelligence_snapshot: dict | None = None) -> None:
    db = connect(path)
    try:
        db.execute("BEGIN IMMEDIATE")
        current = db.execute("SELECT value FROM metadata WHERE key='catalog_identity'").fetchone()
        if current and json.loads(current[0]) != metadata.get("catalog_identity"):
            exists = db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='qualifications'").fetchone()
            if exists:
                db.execute("DELETE FROM qualifications")
        db.execute("DELETE FROM functions")
        db.execute("DELETE FROM edges")
        for index, row in enumerate(functions, 1):
            db.execute("INSERT INTO functions VALUES (?, ?, ?)",
                       (row["address"], row["cache_key"], canonical(row)))
            if fail_after is not None and index == fail_after:
                raise RuntimeError("injected interrupted catalog write")
        db.executemany("INSERT INTO edges VALUES (?, ?, ?, ?, ?, ?)",
                       [(e["caller"], e["callee"], e["kind"], e["reference"], e["source"], e["confidence"])
                        for e in edges])
        db.execute("DELETE FROM metadata")
        db.executemany("INSERT INTO metadata VALUES (?, ?)",
                       [(key, canonical(value)) for key, value in metadata.items()])
        db.execute("DELETE FROM intelligence_snapshot")
        if intelligence_snapshot is not None:
            db.execute("INSERT INTO intelligence_snapshot VALUES (1, ?)", (canonical(intelligence_snapshot),))
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


def load_snapshot(path: Path) -> tuple[list[dict], list[dict], dict]:
    if not path.exists():
        raise ValueError("catalog has not been built")
    db = connect(path)
    try:
        functions = [json.loads(row[0]) for row in db.execute("SELECT data FROM functions ORDER BY address")]
        edges = [{"caller": a, "callee": b, "kind": c, "reference": d, "source": e, "confidence": f}
                 for a, b, c, d, e, f in db.execute(
                     "SELECT caller,callee,kind,reference,source,confidence FROM edges ORDER BY caller,kind,callee")]
        metadata = {key: json.loads(value) for key, value in db.execute("SELECT key,value FROM metadata")}
        if not metadata:
            raise ValueError("catalog has not been built")
        return functions, edges, metadata
    finally:
        db.close()


def graph(edges: list[dict]) -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    forward: dict[str, set[str]] = defaultdict(set)
    reverse: dict[str, set[str]] = defaultdict(set)
    for edge in edges:
        if edge["callee"] and edge["kind"] in ("direct", "indirect"):
            forward[edge["caller"]].add(edge["callee"])
            reverse[edge["callee"]].add(edge["caller"])
    return forward, reverse


def strongly_connected_components(nodes: set[str], adjacency: dict[str, set[str]]) -> list[list[str]]:
    # Iterative Kosaraju avoids Python's recursion limit on long dependency chains.
    reverse: dict[str, set[str]] = defaultdict(set)
    for caller, callees in adjacency.items():
        for callee in callees:
            reverse[callee].add(caller)
    visited: set[str] = set()
    finish_order: list[str] = []
    for start in sorted(nodes):
        if start in visited:
            continue
        visited.add(start)
        stack = [(start, iter(sorted(adjacency.get(start, ()))))]
        while stack:
            node, targets = stack[-1]
            target = next(targets, None)
            if target is None:
                finish_order.append(node)
                stack.pop()
            elif target not in visited:
                visited.add(target)
                stack.append((target, iter(sorted(adjacency.get(target, ())))))
    visited.clear()
    result: list[list[str]] = []
    for start in reversed(finish_order):
        if start in visited:
            continue
        component = []
        todo = [start]
        visited.add(start)
        while todo:
            node = todo.pop()
            component.append(node)
            for target in sorted(reverse.get(node, ())):
                if target not in visited:
                    visited.add(target)
                    todo.append(target)
        result.append(sorted(component))
    return sorted(result, key=lambda part: part[0])


def weak_clusters(edges: list[dict], known_nodes: set[str] | None = None) -> list[list[str]]:
    adjacency: dict[str, set[str]] = defaultdict(set)
    nodes = set(known_nodes or ())
    for edge in edges:
        if edge["callee"] and edge["kind"] in ("direct", "indirect"):
            a, b = edge["caller"], edge["callee"]
            adjacency[a].add(b); adjacency[b].add(a); nodes.update((a, b))
    clusters = []
    while nodes:
        start = min(nodes)
        todo, found = [start], set()
        while todo:
            node = todo.pop()
            if node in found:
                continue
            found.add(node)
            todo.extend(adjacency[node] - found)
        nodes -= found
        clusters.append(sorted(found))
    return sorted(clusters, key=lambda part: (-len(part), part[0]))


def rank_functions(functions: list[dict], edges: list[dict], limit: int = 50,
                   configured_weights: dict | None = None) -> list[dict]:
    if limit < 0:
        raise ValueError("rank limit must not be negative")
    if configured_weights is not None and not isinstance(configured_weights, dict):
        raise ValueError("ranking weights must be a JSON object")
    weights = {**DEFAULT_RANK_WEIGHTS, **(configured_weights or {})}
    unknown_weights = set(weights) - set(DEFAULT_RANK_WEIGHTS)
    if unknown_weights or any(not isinstance(value, (int, float)) or not math.isfinite(value)
                              or value < 0 for value in weights.values()):
        raise ValueError("ranking weights must be nonnegative numbers for known components")
    if not any(weights.values()):
        raise ValueError("at least one ranking weight must be positive")
    forward, reverse = graph(edges)
    results = []
    for row in functions:
        if row["replacement_status"] in ("replaced", "edge-verified", "verified"):
            continue
        ownership = row["ownership"]["status"]
        if ownership in {"claimed", "contributor_owned", "active_automation_worker", "already_implemented"}:
            continue
        reasons = []
        component_scores = {}
        known = 0

        def add(component: str, ratio: float, evidence_known: bool, reason: str) -> None:
            nonlocal known
            weight = weights[component]
            component_scores[component] = {"weight": weight, "evidence_ratio": round(ratio, 3),
                                           "weighted_score": round(weight * ratio, 3)}
            reasons.append(f"{component} ({weight:g} weight): {reason}")
            known += int(evidence_known)

        size = row["size_bytes"]
        if size is None:
            add("feasibility", 0, False, "function size is unknown")
        else:
            ratio = 1 if size <= 64 else 0.8 if size <= 256 else 0.55 if size <= 1024 else 0.2
            add("feasibility", ratio, True,
                f"{size} bytes; size is an execution-feasibility proxy, not a guarantee")
        call_info = row["calls"]
        if call_info["analysis_complete"]:
            count = (len(call_info["direct"]) + len(call_info["indirect"])
                     + len(call_info["imports"]) + len(call_info["unresolved_direct"])
                     + len(call_info["unresolved_indirect"]))
            memory_complete = row.get("memory_dependencies", {}).get("complete", False)
            ratio = 1 if count == 0 else 0.8 if count <= 3 else 0.53 if count <= 10 else 0.2
            if not memory_complete:
                ratio = min(ratio, 0.25)
            add("dependency", ratio, memory_complete,
                f"static call scan records {count} call sites; memory/global dependency completeness is "
                + ("established" if memory_complete else "unknown"))
        else:
            add("dependency", 0.13, False, "call/dependency scan is unknown; conservative low score")
        if row["fixture_difficulty"] != "unknown":
            ratio = {"low": 1, "medium": 0.6, "high": 0.2}[row["fixture_difficulty"]]
            add("fixture_difficulty", ratio, True,
                f"assessed as {row['fixture_difficulty']} by analysis metadata")
        elif row["instruction_count"] is not None:
            ratio = 0.5 if row["instruction_count"] <= 12 else 0.35 if row["instruction_count"] <= 40 else 0.2
            add("fixture_difficulty", ratio, False,
                f"estimate unknown; {row['instruction_count']} instructions used only as a size proxy")
        else:
            add("fixture_difficulty", 0.1, False, "instruction count and fixture difficulty are unknown")
        qualification = row.get("qualification") or {}
        if qualification.get("boundary_class") == "cross_tool_supported":
            add("analysis_confidence", 1, True, "ELF extent and independent decoder corroborate the Ghidra entry")
        elif row["boundary"]["status"] == "ghidra-defined":
            add("analysis_confidence", 0.1, False,
                f"Ghidra-defined extent only ({row['boundary']['evidence']}); independent support is pending")
        else:
            add("analysis_confidence", 0, False, "boundary evidence is unknown")
        beneficiaries = len(reverse.get(row["address"], ()))
        if call_info["analysis_complete"]:
            add("unlock", min(1, beneficiaries / 5), True,
                f"known callers depending on this function: {beneficiaries}")
        else:
            add("unlock", 0.07, False, "potential unknown because call graph coverage is incomplete")
        if row["runtime_relevance"] is not None:
            ratio = row["runtime_relevance"]["score"] / 100
            add("runtime_relevance", ratio, True,
                f"score {row['runtime_relevance']['score']}/100 from {row['runtime_relevance']['source']}")
        elif row["system"]:
            add("runtime_relevance", 1 / 3, False,
                f"tracked subsystem {row['system']}; boot relevance is not established")
        else:
            add("runtime_relevance", 0.07, False, "subsystem and runtime/boot relevance are unknown")
        if ownership in {"no_recorded_claim", "confirmed_unclaimed"}:
            add("ownership", 1, True, "all configured ownership sources were checked; no claim matched")
        else:
            add("ownership", 0.2, False, "evidence is unavailable; confirm before assignment")
        if qualification.get("eligible_for_original_execution"):
            add("original_execution", 1, True, "candidate passed the bounded original-instruction safety gates")
        elif qualification:
            add("original_execution", 0.1, False,
                "qualification attempted; blockers remain: " + "; ".join(qualification.get("blockers", [])))
        else:
            add("original_execution", 0.1, False, "not yet examined by the qualification campaign")
        if size is not None and row.get("instruction_count") is not None:
            effort_ratio = 1 if size <= 32 else 0.7 if size <= 128 else 0.3
            add("engineering_effort", effort_ratio, False,
                f"{size} bytes and {row['instruction_count']} instructions; this is only a size estimate")
        else:
            add("engineering_effort", 0.1, False, "size estimate is unavailable")
        if qualification.get("eligible_for_original_execution"):
            add("test_infrastructure", 1, True, "the generic original-code harness path is available")
        else:
            add("test_infrastructure", 0.2, False,
                "candidate-specific harness readiness has not been established")
        context = row.get("type_intelligence", {})
        if context.get("names") and not row.get("classification", {}).get("conflict"):
            add("type_context", 0.6, True, "type names and explicit relationships aid investigation; ABI and behavior remain unproven")
        elif context.get("relations"):
            add("type_context", 0.2, False, "table references exist but named type/relationship confidence is incomplete")
        else:
            add("type_context", 0, False, "no recovered type evidence")
        score = round(sum(item["weighted_score"] for item in component_scores.values()), 3)
        results.append({"address": row["address"], "name": row["name"], "system": row["system"],
                        "score": score, "confidence_percent": round(100 * known / len(weights)),
                        "eligibility": "eligible" if ownership in {"no_recorded_claim", "confirmed_unclaimed"} else "conditional",
                        "ownership": ownership, "component_scores": component_scores, "reasons": reasons})
    return sorted(results, key=lambda item: (-item["score"], -item["confidence_percent"], item["address"]))[:limit]


def _json(value: object, as_json: bool) -> None:
    if as_json:
        print(json.dumps(value, indent=2, sort_keys=True))
    else:
        if isinstance(value, dict):
            for key, item in value.items():
                print(f"{key}: {json.dumps(item, sort_keys=True)}")
        elif isinstance(value, list):
            for item in value:
                print(json.dumps(item, sort_keys=True))
        else:
            print(value)


def current_qualification_records(state: Path, root: Path) -> list[dict]:
    """Return only cached decisions made by the current qualification toolchain."""
    import function_qualification as qualification
    cached = qualification.load_qualifications(state)
    if not cached:
        return []
    try:
        tool_identity = qualification.qualification_tool_identity(root)
    except (OSError, ValueError):
        return []
    return qualification.load_qualifications(state, tool_identity)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Build and query Paleblood's evidence-aware function catalog")
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    p.add_argument("--state", type=Path, default=default_state_path(), help="local SQLite state path")
    p.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    sub = p.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build", help="import tracked function metadata and optional analysis artifacts")
    build.add_argument("--executable", type=Path, help="local executable; only its SHA-256 is stored")
    build.add_argument("--edges", type=Path, help="Ghidra call edge CSV")
    build.add_argument("--metrics", type=Path, help="Ghidra function analysis CSV")
    build.add_argument("--claims", type=Path, help="local JSON snapshot of active ownership claims")
    build.add_argument("--relevance", type=Path, help="evidence CSV with per-function runtime/boot relevance")
    build.add_argument("--captures", type=Path, help="local capture provenance JSON; capture bytes are never imported")
    build.add_argument("--analysis-version", default="unknown", help="Ghidra and analysis script version")
    build.add_argument("--claims-complete", action="store_true", help="claim snapshot covers current open issue/PR inventory")
    build.add_argument("--config", type=Path, help="JSON analysis configuration to include in cache identity")
    status = sub.add_parser("status", help="show catalog coverage and cache identity")
    fn = sub.add_parser("function", help="show one function record")
    fn.add_argument("address")
    deps = sub.add_parser("dependencies", help="show known forward/reverse dependencies")
    deps.add_argument("address")
    rank = sub.add_parser("rank", help="rank unimplemented candidates")
    rank.add_argument("--limit", type=int, default=50)
    rank.add_argument("--weights", type=Path, help="JSON object overriding default component weights")
    leaves = sub.add_parser("leaves", help="list analyzed functions with no known internal callees")
    leaves.add_argument("--limit", type=int, default=100)
    unresolved = sub.add_parser("unresolved", help="list unresolved call sites from complete call scans")
    unresolved.add_argument("address", nargs="?")
    cluster = sub.add_parser("cluster", aliases=["clusters"], help="show known call graph SCCs and dependency clusters")
    cluster.add_argument("address", nargs="?")
    cluster.add_argument("--eligible", action="store_true", help="restrict clusters to qualified execution candidates")
    export = sub.add_parser("export", help="write a portable JSON catalog")
    export.add_argument("output", type=Path)
    ownership = sub.add_parser("ownership", help="synchronize repository and local work ownership")
    ownership_sub = ownership.add_subparsers(dest="ownership_command", required=True)
    ownership_sync = ownership_sub.add_parser("sync", help="check all available ownership sources")
    ownership_sync.add_argument("--repo", help="GitHub owner/repository (default: parse origin remote)")
    ownership_sync.add_argument("--output", type=Path, help="optional path for a local evidence snapshot")
    qualify = sub.add_parser("qualify", help="run a bounded candidate qualification campaign")
    qualify.add_argument("--batch-size", type=int, default=20)
    qualify.add_argument("--executable", type=Path, required=True, help="local ELF used by this catalog")
    qualify.add_argument("--image-base", type=lambda value: int(value, 0), default=0x400000)
    qualify.add_argument("--refresh", action="store_true", help="ignore cached qualification decisions")
    eligible = sub.add_parser("eligible", help="list qualified candidates at one milestone")
    eligible.add_argument("--stage", choices=("fixture-generation", "original-execution", "replacement-development"),
                          default="original-execution")
    explain = sub.add_parser("explain", help="explain one candidate's qualification")
    explain.add_argument("address")
    blockers = sub.add_parser("blockers", help="summarize blockers in the latest campaign")
    blockers.add_argument("--limit", type=int, default=30)
    intelligence = sub.add_parser("intelligence", help="import/query local type, table and classification evidence")
    intel_sub = intelligence.add_subparsers(dest="intelligence_command", required=True)
    intel_import = intel_sub.add_parser("import")
    intel_import.add_argument("artifact", type=Path)
    intel_import.add_argument("--community-mappings", type=Path)
    intel_sub.add_parser("status")
    for kind in ("types", "vtables"):
        query = intel_sub.add_parser(kind)
        query.add_argument("--limit", type=int, default=20)
    sub.add_parser("invalidate", help="invalidate cached state after input or assumption changes")
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "build":
            executable_sha = sha256(args.executable) if args.executable else None
            config = json.loads(args.config.read_text(encoding="utf-8")) if args.config else {}
            if (args.edges or args.metrics or args.relevance or args.captures) and not args.executable:
                raise ValueError("--executable is required when importing analysis artifacts")
            if (args.edges or args.metrics or args.relevance or args.captures) and args.analysis_version == "unknown":
                raise ValueError("--analysis-version is required when importing analysis artifacts")
            funcs, edges, metadata = build_rows(args.root, executable_sha, args.edges, args.metrics,
                                                args.claims, args.claims_complete, args.relevance,
                                                args.captures, args.analysis_version, config)
            write_snapshot(args.state, funcs, edges, metadata)
            _json(metadata, args.json)
            return 0
        if args.command == "invalidate":
            db = connect(args.state)
            try:
                db.execute("BEGIN IMMEDIATE")
                db.execute("DELETE FROM functions"); db.execute("DELETE FROM edges"); db.execute("DELETE FROM metadata")
                for table in ("qualifications", "ownership_snapshots", "intelligence_snapshot"):
                    if db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)).fetchone():
                        db.execute(f"DELETE FROM {table}")
                db.commit()
                result = {"invalidated": "all", "reason": "input or analysis assumptions changed"}
            finally:
                db.close()
            _json(result, args.json)
            return 0
        if args.command == "intelligence":
            import function_intelligence as intelligence
            if args.intelligence_command == "import":
                result = intelligence.import_artifact(args.state, args.artifact, args.community_mappings)
            elif args.intelligence_command == "status":
                _, _, metadata = load_snapshot(args.state)
                result = metadata.get("intelligence_summary", {"status": "unavailable"})
            else:
                if args.limit < 0:
                    raise ValueError("limit must be nonnegative")
                snapshot = intelligence.load_artifact(args.state)
                result = snapshot.get("artifact", {}).get(args.intelligence_command, [])[:args.limit]
            _json(result, args.json)
            return 0
        if args.command == "ownership":
            import function_qualification as qualification
            repo = args.repo or qualification.repository_slug(args.root)
            snapshot = qualification.synchronize_ownership(args.root, repo)
            qualification.cache_snapshot(args.state, snapshot)
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                temp_name = None
                try:
                    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=args.output.parent,
                                                     prefix=args.output.name + ".", suffix=".tmp", delete=False) as temp:
                        temp_name = temp.name
                        temp.write(json.dumps(snapshot, indent=2, sort_keys=True) + "\n")
                        temp.flush()
                        os.fsync(temp.fileno())
                    os.replace(temp_name, args.output)
                    temp_name = None
                finally:
                    if temp_name:
                        os.unlink(temp_name)
            _json(snapshot, args.json)
            return 0
        if args.command == "qualify":
            import function_qualification as qualification
            functions, edges, metadata = load_snapshot(args.state)
            executable_sha, segments = qualification.read_elf_executable_segments(args.executable, args.image_base)
            if not metadata.get("executable_sha256") or executable_sha != metadata["executable_sha256"]:
                raise ValueError("local executable SHA-256 does not match the built analysis catalog")
            snapshot = qualification.load_ownership(args.state)
            if not qualification.ownership_snapshot_is_current(snapshot):
                raise ValueError("ownership snapshot is incomplete, untimestamped, or older than 24 hours; "
                                 "run ownership sync successfully before qualifying")
            metadata["qualification_tools"] = qualification.qualification_tool_identity(args.root)
            previous = {} if args.refresh else {item["address"]: item
                                                 for item in qualification.load_qualifications(
                                                     args.state, metadata["qualification_tools"])}
            decisions = qualification.qualify_rows(functions, edges, snapshot, executable_sha,
                                                   args.executable, segments, metadata, args.batch_size, previous)
            qualification.save_qualifications(args.state, decisions)
            counts: dict[str, int] = defaultdict(int)
            for item in decisions:
                counts[item["classification"]] += 1
            _json({"catalog_identity": metadata.get("catalog_identity"), "executable_sha256": executable_sha,
                   "analyzed": len(decisions), "qualification_counts": dict(sorted(counts.items())),
                   "results": decisions}, args.json)
            return 0
        functions, edges, metadata = load_snapshot(args.state)
        by_address = {row["address"]: row for row in functions}
        if args.command == "status":
            statuses: dict[str, int] = defaultdict(int)
            for row in functions:
                statuses[row["replacement_status"]] += 1
            result = {**metadata, "status_counts": dict(sorted(statuses.items())),
                      "known_edges": len(edges),
                      "functions_with_static_call_scan": sum(r["calls"]["analysis_complete"] for r in functions),
                      "functions_with_unknown_call_graph": sum(not r["calls"]["analysis_complete"] for r in functions),
                      "functions_with_incomplete_memory_dependencies": sum(
                          not r["memory_dependencies"]["complete"] for r in functions),
                      "instruction_count_known": sum(r["instruction_count"] is not None for r in functions),
                      "ownership_known": sum(r["ownership"]["status"] != "unknown" for r in functions),
                      "replacement_coverage": statuses.get("replaced", 0) + statuses.get("edge-verified", 0)
                      + statuses.get("verified", 0),
                      "phase1_strictly_eligible_candidates": metadata.get("strictly_eligible_candidates", 0),
                      "conditional_candidates": sum(
                          r["replacement_status"] not in ("replaced", "edge-verified", "verified")
                          and r["ownership"]["status"] == "unknown" for r in functions)}
            try:
                import function_qualification as qualification
                owner_snapshot = qualification.load_ownership(args.state)
                owner_counts: dict[str, int] = defaultdict(int)
                if owner_snapshot.get("snapshot_complete"):
                    by_addr: dict[str, set[str]] = defaultdict(set)
                    by_name: dict[str, set[str]] = defaultdict(set)
                    for claim in owner_snapshot.get("claims", []):
                        if claim.get("address"):
                            by_addr[claim["address"]].add(claim.get("kind", "unknown"))
                        if claim.get("symbol"):
                            by_name[claim["symbol"]].add(claim.get("kind", "unknown"))
                    by_system: dict[str, set[str]] = defaultdict(set)
                    for claim in owner_snapshot.get("claims", []):
                        if claim.get("system"):
                            by_system[claim["system"]].add(claim.get("kind", "unknown"))
                    for function in functions:
                        if function["replacement_status"] in ("replaced", "edge-verified", "verified"):
                            owner_counts["already_implemented"] += 1
                        else:
                            kinds = (by_addr.get(function["address"], set())
                                     | by_name.get(function.get("name"), set())
                                     | by_system.get(function.get("system"), set()))
                            has_contributor = bool(kinds & {"contributor", "active_pr", "repository_claim"})
                            has_local = "local_assignment" in kinds
                            if has_contributor and has_local:
                                owner_counts["ownership_conflict"] += 1
                            elif has_contributor:
                                owner_counts["contributor_owned"] += 1
                            elif has_local:
                                owner_counts["active_automation_worker"] += 1
                            else:
                                owner_counts["confirmed_unclaimed"] += 1
                else:
                    owner_counts["unknown"] = len(functions)
                qualified = current_qualification_records(args.state, args.root)
                result["ownership_source_snapshot_complete"] = owner_snapshot.get("snapshot_complete", False)
                result["ownership_source_snapshot_fresh"] = qualification.ownership_snapshot_is_current(owner_snapshot)
                result["ownership_source_checks"] = owner_snapshot.get("sources", [])
                result["ownership_classification_counts"] = dict(sorted(owner_counts.items()))
                result["ownership_resolved"] = len(functions) - owner_counts.get("unknown", 0)
                result["ownership_known"] = result["ownership_resolved"]
                result["conditional_candidates"] = owner_counts.get("unknown", 0)
                result["confirmed_unclaimed_candidates"] = owner_counts.get("confirmed_unclaimed", 0)
                result["independently_confirmed_boundaries"] = metadata.get("independently_confirmed_boundaries", 0)
                result["qualification_records"] = len(qualified)
                result["cross_tool_supported_boundaries_in_campaign"] = sum(
                    row.get("boundary_class") == "cross_tool_supported" for row in qualified)
                result["qualification_classification_counts"] = {
                    key: sum(row.get("classification") == key for row in qualified)
                    for key in sorted({row.get("classification") for row in qualified})}
                result["qualified_for_fixture_generation"] = sum(
                    row.get("eligible_for_fixture_generation", False) for row in qualified)
                result["qualified_for_original_execution"] = sum(
                    row.get("eligible_for_original_execution", False) for row in qualified)
                result["qualified_for_replacement_development"] = sum(
                    row.get("eligible_for_replacement_development", False) for row in qualified)
                result["strictly_eligible_candidates"] = result["qualified_for_original_execution"]
            except (ImportError, sqlite3.Error):
                result["qualification_state"] = "unavailable"
            _json(result, args.json)
        elif args.command == "function":
            _json(by_address.get(address(args.address), {"address": address(args.address), "status": "not found"}), args.json)
        elif args.command == "dependencies":
            key = address(args.address)
            forward, reverse = graph(edges)
            row = by_address.get(key)
            out = {"address": key, "analysis_complete": row["calls"]["analysis_complete"] if row else False,
                   "known_callees": sorted(forward.get(key, ())), "known_callers": sorted(reverse.get(key, ())),
                   "callees": sorted(forward.get(key, ())) if row and row["calls"]["analysis_complete"] else None,
                   "callers": sorted(reverse.get(key, ())) if row and row["calls"]["analysis_complete"] else None,
                   "unresolved_direct": row["calls"]["unresolved_direct"] if row and row["calls"]["analysis_complete"] else None,
                   "unresolved_indirect": row["calls"]["unresolved_indirect"] if row and row["calls"]["analysis_complete"] else None,
                   "referenced_imports": row["calls"]["imports"] if row and row["calls"]["analysis_complete"] else None}
            _json(out, args.json)
        elif args.command == "rank":
            weights = json.loads(args.weights.read_text(encoding="utf-8")) if args.weights else None
            import function_qualification as qualification
            decisions = {item["address"]: item for item in current_qualification_records(args.state, args.root)}
            owner_snapshot = qualification.load_ownership(args.state)
            claims_by_address: dict[str, list[dict]] = defaultdict(list)
            claims_by_symbol: dict[str, list[dict]] = defaultdict(list)
            claims_by_system: dict[str, list[dict]] = defaultdict(list)
            for claim in owner_snapshot.get("claims", []):
                if claim.get("address"):
                    claims_by_address[claim["address"]].append(claim)
                if claim.get("symbol"):
                    claims_by_symbol[claim["symbol"]].append(claim)
                if claim.get("system"):
                    claims_by_system[claim["system"]].append(claim)
            ranked_functions = []
            for row in functions:
                enriched = dict(row)
                enriched["qualification"] = decisions.get(row["address"], {})
                if owner_snapshot.get("snapshot_complete"):
                    matches = (claims_by_address.get(row["address"], [])
                               + claims_by_symbol.get(row.get("name"), [])
                               + claims_by_system.get(row.get("system"), []))
                    kinds = {claim.get("kind") for claim in matches}
                    if row["replacement_status"] in {"replaced", "edge-verified", "verified"} or "implemented" in kinds:
                        ownership_status = "already_implemented"
                    elif kinds & {"contributor", "active_pr", "repository_claim"}:
                        ownership_status = "contributor_owned"
                    elif "local_assignment" in kinds:
                        ownership_status = "active_automation_worker"
                    else:
                        ownership_status = "confirmed_unclaimed"
                    enriched["ownership"] = {**row["ownership"], "status": ownership_status}
                ranked_functions.append(enriched)
            _json(rank_functions(ranked_functions, edges, args.limit, weights), args.json)
        elif args.command == "leaves":
            forward, _ = graph(edges)
            known = [row for row in functions if row["calls"]["analysis_complete"]
                     and not forward.get(row["address"])]
            known.sort(key=lambda row: row["address"])
            _json({"analyzed_leaf_count": len(known), "unknown_dependency_count":
                   sum(not row["calls"]["analysis_complete"] for row in functions),
                   "functions": known[:args.limit]}, args.json)
        elif args.command == "unresolved":
            selected = [row for row in functions if row["calls"]["analysis_complete"]
                        and (not args.address or row["address"] == address(args.address))
                        and (row["calls"]["unresolved_direct"] or row["calls"]["unresolved_indirect"])]
            _json(selected, args.json)
        elif args.command in ("cluster", "clusters"):
            forward, _ = graph(edges)
            eligible_addresses = None
            if args.eligible:
                import function_qualification as qualification
                eligible_addresses = {item["address"] for item in current_qualification_records(args.state, args.root)
                                      if item.get("eligible_for_original_execution")}
                edges = [edge for edge in edges if edge["caller"] in eligible_addresses
                         and edge.get("callee") in eligible_addresses]
                forward, _ = graph(edges)
            graph_nodes = {row["address"] for row in functions if row["calls"]["analysis_complete"]}
            if eligible_addresses is not None:
                graph_nodes &= eligible_addresses
            graph_nodes.update(forward)
            for targets in forward.values(): graph_nodes.update(targets)
            sccs = strongly_connected_components(graph_nodes, forward)
            cyclic = [part for part in sccs if len(part) > 1 or part[0] in forward.get(part[0], set())]
            clusters = weak_clusters(edges, graph_nodes)
            if args.address:
                key = address(args.address)
                cluster_result = next((part for part in clusters if key in part), [])
                scc_result = next((part for part in sccs if key in part), [])
                row = by_address.get(key)
                _json({"address": key, "analysis_complete": row["calls"]["analysis_complete"] if row else False,
                       "scc": scc_result or None, "cluster": cluster_result or None,
                       "graph_coverage": metadata.get("functions_with_static_call_scan", 0)}, args.json)
            else:
                nontrivial_clusters = [part for part in clusters if len(part) > 1]
                _json({"strongly_connected_component_count": len(sccs),
                       "cyclic_components": cyclic,
                       "weak_dependency_clusters": nontrivial_clusters,
                       "weak_dependency_cluster_count": len(nontrivial_clusters),
                       "analyzed_isolated_functions": sum(len(part) == 1 for part in clusters),
                       "analyzed_leaf_count": sum(row["calls"]["analysis_complete"]
                           and not forward.get(row["address"]) for row in functions),
                       "cluster_count": len(clusters), "eligible_filter": args.eligible,
                       "edge_analysis_incomplete":
                       metadata.get("functions_with_static_call_scan", 0) < len(functions)}, args.json)
        elif args.command == "export":
            args.output.parent.mkdir(parents=True, exist_ok=True)
            snapshot = {"schema_version": SCHEMA_VERSION, "metadata": metadata,
                        "functions": functions, "edges": edges}
            temp_name = None
            try:
                with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=args.output.parent,
                                                 prefix=args.output.name + ".", suffix=".tmp", delete=False) as temp:
                    temp_name = temp.name
                    temp.write(json.dumps(snapshot, indent=2, sort_keys=True) + "\n")
                    temp.flush()
                    os.fsync(temp.fileno())
                os.replace(temp_name, args.output)
                temp_name = None
            finally:
                if temp_name:
                    os.unlink(temp_name)
            _json({"exported": str(args.output), "functions": len(functions), "edges": len(edges)}, args.json)
        elif args.command in ("eligible", "explain", "blockers"):
            import function_qualification as qualification
            decisions = current_qualification_records(args.state, args.root)
            by_decision = {item["address"]: item for item in decisions}
            if args.command == "eligible":
                stage_field = {"fixture-generation": "eligible_for_fixture_generation",
                               "original-execution": "eligible_for_original_execution",
                               "replacement-development": "eligible_for_replacement_development"}[args.stage]
                _json([item for item in decisions if item.get(stage_field)], args.json)
            elif args.command == "explain":
                key = address(args.address)
                explanation = by_decision.get(key)
                if explanation is None:
                    import function_qualification as qualification
                    row = by_address.get(key)
                    snapshot = qualification.load_ownership(args.state)
                    owner = (qualification.classify_ownership(key, row.get("name") if row else None, snapshot,
                                                              row.get("system") if row else None)
                             if row else {"status": "unknown", "blockers": ["address is absent from the catalog"]})
                    explanation = {"address": key, "classification": "not_yet_analyzed",
                                   "reason": "candidate is outside the latest qualification batch",
                                   "ownership": owner,
                                   "catalog_function": ({"size_bytes": row.get("size_bytes"),
                                                        "replacement_status": row.get("replacement_status"),
                                                        "boundary": row.get("boundary"),
                                                        "instruction_count": row.get("instruction_count"),
                                                        "calls": row.get("calls"),
                                                        "memory_dependencies": row.get("memory_dependencies")}
                                                       if row else None)}
                _json(explanation, args.json)
            else:
                blockers: dict[str, int] = defaultdict(int)
                for item in decisions:
                    for reason in item.get("blockers", []):
                        blockers[reason] += 1
                summary = sorted(({"reason": reason, "count": count} for reason, count in blockers.items()),
                                 key=lambda item: (-item["count"], item["reason"]))
                import function_qualification as qualification
                snapshot = qualification.load_ownership(args.state)
                claim_addresses = {claim.get("address") for claim in snapshot.get("claims", [])
                                   if claim.get("address")}
                claim_names = {claim.get("symbol") for claim in snapshot.get("claims", [])
                               if claim.get("symbol")}
                claim_systems = {claim.get("system") for claim in snapshot.get("claims", [])
                                 if claim.get("system")}
                ownership_counts: dict[str, int] = defaultdict(int)
                for row in functions:
                    if row["replacement_status"] in ("replaced", "edge-verified", "verified"):
                        ownership_counts["already_implemented"] += 1
                    elif (row["address"] in claim_addresses or row.get("name") in claim_names
                          or row.get("system") in claim_systems):
                        ownership_counts["claimed_or_assigned"] += 1
                    elif snapshot.get("snapshot_complete"):
                        ownership_counts["confirmed_unclaimed"] += 1
                    else:
                        ownership_counts["unknown"] += 1
                _json({"analyzed": len(decisions), "ownership_snapshot_complete": snapshot.get("snapshot_complete", False),
                       "ownership_counts": dict(sorted(ownership_counts.items())),
                       "qualification_blockers": summary[:args.limit]}, args.json)
        return 0
    except (OSError, ValueError, sqlite3.Error, json.JSONDecodeError) as error:
        print(f"function_catalog: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
