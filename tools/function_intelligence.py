#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Local type relationships and conservative function classification, never behavior evidence."""
from __future__ import annotations

from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import subprocess

import function_catalog as catalog

VERSION = "1"
CATEGORIES = ("game_engine", "middleware", "runtime_library", "compiler_generated", "unknown")
CONFIDENCES = {"structural_candidate", "symbol_supported", "analysis_supported", "candidate"}
ROLES = {"virtual_slot", "vtable_reference_candidate", "constructor_symbol_candidate",
         "destructor_symbol_candidate", "class_namespace"}


def text(value, *, nullable=False):
    if nullable and value is None:
        return None
    if not isinstance(value, str) or not value or len(value) > 4096 or any(c in value for c in "\n\r\0"):
        raise ValueError("intelligence text must be a nonempty bounded single line")
    if value.startswith(("/", "file:")) or "/home/" in value or "/mnt/" in value:
        raise ValueError("intelligence must not contain local filesystem paths")
    return value


def evidence(item):
    if item.get("confidence") not in CONFIDENCES:
        raise ValueError("relationship needs an explicit supported confidence label")
    return {"source": text(item.get("source")), "confidence": item["confidence"]}


def unique(items):
    return [json.loads(raw) for raw in sorted({catalog.canonical(item) for item in items})]


def demangle_names(types: list[dict]) -> dict[str, str]:
    names = sorted({text(item["encoded_name"]) for item in types if item.get("encoded_name")})
    if not names:
        return {}
    try:
        result = subprocess.run(["c++filt", "-t"], input="\n".join(names) + "\n", text=True,
                                capture_output=True, check=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return {}  # Encoded names remain available; decoding is not guessed.
    lines = result.stdout.splitlines()
    if len(lines) != len(names):
        raise ValueError("type-name demangler returned an inconsistent record count")
    return {name: text(decoded) for name, decoded in zip(names, lines) if name != decoded}


def normalize(raw: dict, executable_sha: str) -> dict:
    allowed = {"schema", "executable_sha256", "analysis_version", "types", "vtables", "relations", "functions", "scan"}
    if set(raw) - allowed or raw.get("schema") != 1:
        raise ValueError("unsupported type-intelligence schema/fields; bytes and listings are forbidden")
    if not executable_sha or raw.get("executable_sha256") != executable_sha:
        raise ValueError("type-intelligence executable SHA-256 does not match the catalog")
    for key in ("types", "vtables", "relations", "functions"):
        if not isinstance(raw.get(key), list):
            raise ValueError(f"type-intelligence {key} must be an array")
    decoded = demangle_names(raw["types"])
    types = defaultdict(list)
    for item in raw["types"]:
        if set(item) - {"id", "address", "name", "encoded_name", "bases", "bases_status", "source", "confidence"}:
            raise ValueError("unsupported type fields")
        entry = {"id": text(item.get("id")), "address": catalog.address(item["address"]) if item.get("address") else None,
                 "name": text(item.get("name") or decoded.get(item.get("encoded_name")), nullable=True),
                 "encoded_name": text(item.get("encoded_name"), nullable=True), **evidence(item), "bases": []}
        entry["bases_status"] = text(item.get("bases_status", "unknown"))
        for base in item.get("bases", []):
            if set(base) - {"type_id", "offset_flags", "source", "confidence"}:
                raise ValueError("unsupported base relationship fields")
            entry["bases"].append({"type_id": text(base.get("type_id")),
                                   "offset_flags": base.get("offset_flags"), **evidence(base)})
        types[entry["id"]].append(entry)
    normalized_types = []
    for key, entries in sorted(types.items()):
        names = sorted({e["name"] for e in entries if e["name"]})
        encoded = sorted({e["encoded_name"] for e in entries if e["encoded_name"]})
        addresses = sorted({e["address"] for e in entries if e["address"]})
        normalized_types.append({"id": key, "names": names, "encoded_names": encoded, "addresses": addresses,
                                 "ambiguous": len(names) > 1 or len(encoded) > 1 or len(addresses) > 1,
                                 "base_discovery": sorted({text(e.get("bases_status", "unknown")) for e in entries}),
                                 "bases": unique([base for e in entries for base in e["bases"]]),
                                 "evidence": unique([evidence(e) for e in entries])})
    tables = []
    for item in raw["vtables"]:
        if set(item) - {"address", "type_id", "offset_to_top", "slots", "extent", "source", "confidence"}:
            raise ValueError("unsupported vtable fields")
        slots = []
        for slot in item.get("slots", []):
            if set(slot) - {"index", "function", "thunk_target"} or not isinstance(slot.get("index"), int) or slot["index"] < 0:
                raise ValueError("invalid vtable slot")
            slots.append({"index": slot["index"], "function": catalog.address(slot["function"]),
                          "thunk_target": catalog.address(slot["thunk_target"]) if slot.get("thunk_target") else None})
        tables.append({"address": catalog.address(item["address"]), "type_id": text(item.get("type_id"), nullable=True),
                       "offset_to_top": item.get("offset_to_top"), "slots": unique(slots),
                       "extent": text(item.get("extent")), **evidence(item)})
    relations = []
    for item in raw["relations"]:
        if set(item) - {"function", "type_id", "vtable", "role", "reference", "source", "confidence"}:
            raise ValueError("unsupported function/type relationship fields")
        if item.get("role") not in ROLES:
            raise ValueError("unknown function/type relationship role")
        relations.append({"function": catalog.address(item["function"]), "type_id": text(item.get("type_id"), nullable=True),
                          "vtable": catalog.address(item["vtable"]) if item.get("vtable") else None,
                          "role": item["role"], "reference": catalog.address(item["reference"]) if item.get("reference") else None,
                          **evidence(item)})
    functions = []
    for item in raw["functions"]:
        if set(item) - {"address", "name", "thunk", "source", "confidence"} or not isinstance(item.get("thunk"), bool):
            raise ValueError("invalid function symbol fields")
        functions.append({"address": catalog.address(item["address"]), "name": text(item.get("name")),
                          "thunk": item["thunk"], **evidence(item)})
    scan = raw.get("scan", {})
    if set(scan) - {"bytes_scanned", "limit_bytes", "truncated", "slot_limit", "limitations"}:
        raise ValueError("unsupported scan metadata")
    for limitation in scan.get("limitations", []):
        text(limitation)
    return {"schema": 1, "executable_sha256": executable_sha, "analysis_version": text(raw.get("analysis_version")),
            "types": normalized_types, "vtables": unique(tables), "relations": unique(relations),
            "functions": unique(functions), "scan": scan}


def load_mappings(path: Path | None) -> list[dict]:
    """Accept optional licensed research labels, always as hypotheses about this target."""
    if path is None:
        return []
    raw = json.loads(path.read_text())
    if set(raw) != {"schema", "mappings"} or raw["schema"] != 1 or not isinstance(raw["mappings"], list):
        raise ValueError("community mappings need schema 1 and a mappings array")
    result = []
    for item in raw["mappings"]:
        if set(item) != {"type_name", "label", "category", "source_url", "license", "revision", "attribution", "source_game"}:
            raise ValueError("mapping needs exact type match, source, license, revision, attribution and source game")
        if item["category"] not in CATEGORIES or not item["source_url"].startswith("https://"):
            raise ValueError("mapping category/source invalid")
        normalized = {key: text(value) for key, value in item.items()}
        if normalized["license"].lower() in {"unknown", "unlicensed", "none", "noassertion"} or "non-ai" in normalized["license"].lower():
            raise ValueError("unlicensed community mappings are not accepted")
        result.append({**normalized, "confidence": "candidate", "target_layout_or_behavior_proven": False})
    return unique(result)


def annotate(functions: list[dict], artifact: dict, mappings: list[dict]) -> tuple[list[dict], dict]:
    by_type = {item["id"]: item for item in artifact["types"]}
    by_function = defaultdict(list)
    for relation in artifact["relations"]:
        by_function[relation["function"]].append(relation)
    for table in artifact["vtables"]:
        for slot in table["slots"]:
            by_function[slot["function"]].append({"type_id": table["type_id"], "vtable": table["address"],
                                                  "role": "virtual_slot", "slot": slot["index"], **evidence(table)})
    labels = defaultdict(list)
    for item in artifact["functions"]:
        labels[item["address"]].append(item)
    updated = []
    for row in functions:
        relations = unique(by_function.get(row["address"], []))
        related = sorted({r["type_id"] for r in relations if r["type_id"] in by_type})
        names = {name for key in related if not by_type[key]["ambiguous"] for name in by_type[key]["names"]}
        hints = [item for item in mappings if item["type_name"] in names]
        assertions = []
        for symbol in labels.get(row["address"], []):
            if symbol["thunk"]:
                assertions.append({"category": "compiler_generated", **evidence(symbol), "name": symbol["name"]})
            elif symbol["name"].startswith("std::") and symbol["confidence"] == "symbol_supported":
                assertions.append({"category": "runtime_library", **evidence(symbol), "name": symbol["name"]})
        categories = {item["category"] for item in assertions}
        conflicts = len(categories | {h["category"] for h in hints}) > 1 or any(by_type[t]["ambiguous"] for t in related)
        category = next(iter(categories)) if len(categories) == 1 and not conflicts else "unknown"
        updated.append({**row, "type_intelligence": {"type_ids": related, "names": sorted(names),
                                                     "relations": relations, "symbols": labels.get(row["address"], [])},
                        "classification": {"category": category, "evidence": assertions,
                                           "candidates": hints, "conflict": conflicts,
                                           "behavior_established": False}})
    counts = Counter(row["classification"]["category"] for row in updated)
    denominator = len(updated)
    summary = {"classes_with_names": sum(bool(t["names"]) and not t["ambiguous"] for t in artifact["types"]),
               "type_records": len(artifact["types"]), "ambiguous_type_records": sum(t["ambiguous"] for t in artifact["types"]),
               "memory_rtti_records": sum(bool(t["addresses"]) for t in artifact["types"]),
               "symbol_namespace_type_records": sum(not t["addresses"] for t in artifact["types"]),
               "independently_confirmed_classes": 0,
               "vtable_candidates": len(artifact["vtables"]),
               "typed_vtable_candidates": sum(t["type_id"] in by_type for t in artifact["vtables"]),
               "functions_with_type_associations": sum(bool(r["type_intelligence"]["type_ids"]) for r in updated),
               "functions_with_table_associations": sum(bool(r["type_intelligence"]["relations"]) for r in updated),
               "classification_denominator": denominator,
               "classification": {key: {"functions": counts[key], "percent": round(100 * counts[key] / denominator, 4) if denominator else 0}
                                  for key in CATEGORIES}, "scan": artifact["scan"]}
    return updated, summary


def import_artifact(state: Path, path: Path, mappings_path: Path | None = None) -> dict:
    functions, edges, metadata = catalog.load_snapshot(state)
    artifact = normalize(json.loads(path.read_text()), metadata.get("executable_sha256"))
    mappings = load_mappings(mappings_path)
    functions, summary = annotate(functions, artifact, mappings)
    base_identity = metadata.get("base_catalog_identity", metadata["catalog_identity"])
    material = {"base_catalog_identity": base_identity, "artifact": artifact, "mappings": mappings,
                "tool_sha256": catalog.sha256(Path(__file__)), "version": VERSION}
    identity = hashlib.sha256(catalog.canonical(material).encode()).hexdigest()
    for row in functions:
        row["cache_key"] = hashlib.sha256(f"{identity}:{row['address']}".encode()).hexdigest()
    metadata.update({"catalog_identity": identity, "base_catalog_identity": base_identity,
                     "intelligence_summary": summary, "intelligence_version": VERSION})
    catalog.write_snapshot(state, functions, edges, metadata,
                           intelligence_snapshot={"artifact": artifact, "mappings": mappings, "identity": identity})
    return summary


def load_artifact(state: Path) -> dict:
    db = catalog.connect(state)
    try:
        row = db.execute("SELECT data FROM intelligence_snapshot WHERE id=1").fetchone()
        return json.loads(row[0]) if row else {}
    finally:
        db.close()
