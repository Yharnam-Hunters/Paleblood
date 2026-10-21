#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export bounded, private REA function dossiers; static drafts never change verification status."""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
VERSION = 1
REQUIRED_TOOLS = {"open_binary", "close_binary", "list_procedures", "procedure_info",
                  "address_to_file_offset", "analyze_function", "export_evidence_bundle"}


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def encoded(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def save(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    if path.is_symlink() or temporary.is_symlink():
        raise ValueError("refusing symlinked evidence/state output")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def private_directory(path: Path, root: Path, image: Path) -> Path:
    path, root, image = path.resolve(), root.resolve(), image.resolve()
    if path == root or root in path.parents:
        raise ValueError("draft output must be outside the repository")
    if path == image or path in image.parents:
        raise ValueError("draft output must not contain the input image")
    return path


def elf_layout(path: Path) -> tuple[list[tuple], str]:
    """Return file-backed executable mappings and the project's loaded-image digest."""
    data = path.read_bytes()
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 62:
        raise ValueError("expected a little-endian x86-64 ELF")
    offset = struct.unpack_from("<Q", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 54)
    if stride != 56 or not 1 <= count <= 256 or offset + count * stride > len(data):
        raise ValueError("invalid ELF program-header table")
    loads = [struct.unpack_from("<IIQQQQQQ", data, offset + i * stride) for i in range(count)]
    loads = [row for row in loads if row[0] in (1, 0x61000010)]
    if not loads or any(row[5] > row[6] or row[2] + row[5] > len(data) for row in loads):
        raise ValueError("invalid ELF load range")
    end = max(row[3] + row[6] for row in loads)
    if end > 256 * 1024 * 1024:
        raise ValueError("loaded image exceeds the supported 256 MiB target bound")
    memory = bytearray(end)
    for row in loads:
        memory[row[3]:row[3] + row[5]] = data[row[2]:row[2] + row[5]]
    return [row for row in loads if row[0] == 1 and row[1] & 1], hashlib.sha256(memory).hexdigest()


def pin(root: Path, file: str) -> str:
    for line in (root / file).read_text().splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("*") == "eboot.elf":
            return parts[0]
    raise ValueError(f"{file}: no eboot.elf target pin")


def validate_image(image: Path, root: Path, provenance: Path | None) -> tuple[list[tuple], dict]:
    mappings, loaded_hash = elf_layout(image)
    image_hash, target_hash = digest(image), pin(root, "target.sha256")
    if loaded_hash != pin(root, "target.image.sha256"):
        raise ValueError("image does not match the pinned loaded image")
    identity = {"image_sha256": image_hash, "loaded_image_sha256": loaded_hash}
    if image_hash != target_hash:
        if provenance is None:
            raise ValueError("derived image requires --provenance from rea_image.py")
        record = json.loads(provenance.read_text())
        expected = {
            "kind": "paleblood_rea_analysis_image", "schema_version": 1,
            "original": {"sha256": target_hash, "loaded_image_sha256": loaded_hash},
            "derived": {"sha256": image_hash, "loaded_image_sha256": loaded_hash},
            "catalog": {"sha256": digest(root / "symbols/ghidra_functions.csv")},
            "names": {"sha256": digest(root / "symbols/functions.csv")},
            "address_transform": {"subtract": 0x400000},
        }
        for key, wanted in expected.items():
            actual = record.get(key)
            if isinstance(wanted, dict):
                if not isinstance(actual, dict) or any(actual.get(k) != v for k, v in wanted.items()):
                    raise ValueError(f"analysis-image provenance mismatch: {key}")
            elif actual != wanted:
                raise ValueError(f"analysis-image provenance mismatch: {key}")
        if record.get("preservation", {}).get("load_segments_equal") is not True:
            raise ValueError("analysis-image provenance does not preserve load segments")
        identity["provenance_sha256"] = digest(provenance)
    return mappings, identity


def file_offset(mappings: list[tuple], address: int) -> int:
    matches = [row[2] + address - row[3] for row in mappings if row[3] <= address < row[3] + row[5]]
    if len(matches) != 1:
        raise ValueError(f"0x{address:x}: not in one file-backed executable PT_LOAD")
    return matches[0]


def evidence(response: dict) -> dict:
    if "error" in response:
        raise RuntimeError(encoded(response["error"]))
    result = response.get("result", {})
    if result.get("isError"):
        raise RuntimeError(encoded(result))
    value = result.get("structuredContent")
    if value is None:
        texts = [item["text"] for item in result.get("content", []) if item.get("type") == "text"]
        if len(texts) != 1:
            raise ValueError("REA response has no unambiguous structured result")
        value = json.loads(texts[0])
    if not isinstance(value, dict):
        raise ValueError("REA structured result is not an object")
    return value


class Client:
    """A sequential MCP stdio peer with bounded response and cancellation draining."""

    def __init__(self, command: list[str], stderr: Path, timeout: float):
        self.stderr = stderr.open("w", encoding="utf-8")
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.stderr, text=True, encoding="utf-8", bufsize=1)
        self.responses = queue.Queue()
        self.sequence = 0
        self.pending = None
        self.timeout = timeout
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        try:
            for line in self.process.stdout:
                self.responses.put(json.loads(line))
        except Exception as error:
            self.responses.put(error)
        finally:
            self.responses.put(EOFError("REA stdout closed"))

    def notify(self, method: str, params: dict) -> None:
        self.process.stdin.write(encoded({"jsonrpc": "2.0", "method": method, "params": params}) + "\n")
        self.process.stdin.flush()

    def _receive(self, timeout: float) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            result = self.responses.get(timeout=max(0, deadline - time.monotonic()))
            if isinstance(result, Exception):
                raise result
            if result.get("id") == self.pending:
                self.pending = None
                return result

    def request(self, method: str, params: dict) -> dict:
        if self.pending is not None:
            raise RuntimeError("REA request still pending; cleanup is unconfirmed")
        self.sequence += 1
        self.pending = self.sequence
        self.process.stdin.write(encoded({"jsonrpc": "2.0", "id": self.sequence,
                                         "method": method, "params": params}) + "\n")
        self.process.stdin.flush()
        try:
            return self._receive(self.timeout)
        except queue.Empty:
            self.notify("notifications/cancelled", {"requestId": self.pending, "reason": "draft timeout"})
            try:
                self._receive(min(self.timeout, 15))
            except queue.Empty:
                pass
            raise TimeoutError(f"REA {method} timed out; pending={self.pending is not None}") from None

    def tool(self, name: str, arguments: dict) -> dict:
        return self.request("tools/call", {"name": name, "arguments": arguments})

    def stop(self) -> None:
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process.stdout.close()
        self.stderr.close()


def run(args: argparse.Namespace, *, command: list[str] | None = None) -> dict:
    root, image = args.root.resolve(), args.image.resolve()
    if args.image_base != 0x400000:
        raise ValueError("catalog image base is fixed at 0x400000; use --analysis-base for a rebased provider")
    output = private_directory(args.output, root, image)
    mappings, identity = validate_image(image, root, args.provenance)
    catalog_path = root / "symbols/ghidra_functions.csv"
    with catalog_path.open(newline="") as stream:
        catalog = sorted({int(row["address"], 0) for row in csv.DictReader(stream)})
    selected = [int(value, 0) for value in args.addresses] if args.addresses else catalog
    if not args.addresses and not args.all:
        raise ValueError("select catalog addresses or pass --all for a bounded catalog pass")
    if set(selected) - set(catalog):
        raise ValueError("requested address is absent from the pinned function catalog")
    for address in selected:
        file_offset(mappings, address - args.image_base)
    identity.update(version=VERSION, catalog_sha256=digest(catalog_path), provider=args.provider,
                    image_base=args.image_base, analysis_base=args.analysis_base, provider_environment={
                        key: os.environ.get(key) for key in ("HOPPER_LOADER_ARGS_JSON", "HOPPER_LAUNCHER_PATH",
                        "GHIDRA_INSTALL_DIR", "REA_IDA_MCP_CONFIG", "REA_KUNA_COMMAND")})
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    state_path = output / "drafts.json"
    if state_path.is_symlink():
        raise ValueError("draft state must not be a symlink")
    state = json.loads(state_path.read_text()) if state_path.exists() and args.resume else None
    if state_path.exists() and not args.resume:
        raise ValueError("draft state already exists; use --resume")
    if state is not None and state.get("identity") != identity:
        raise ValueError("resume identity changed; choose a new private output directory")
    state = state or {"identity": identity, "functions": {}, "runs": []}
    # Dossiers require byte-identical evidence. Other attempted statuses advance a sweep unless
    # explicitly retried, so an early missing procedure cannot starve later catalog entries.
    cached = set()
    for address, row in state["functions"].items():
        if row.get("status") == "retained-transport-limited" and int(address, 0) in selected:
            raise ValueError("transport-limited analysis is already retained; inspect the exported Evidence bundle before retrying")
        if row.get("status") != "dossier":
            continue
        path = output / row["file"]
        if not path.resolve().is_relative_to(output) or not path.is_file() or digest(path) != row["sha256"]:
            raise ValueError("resume evidence changed or is missing; choose a new output directory")
        cached.add(int(address, 0))
    attempted = {int(address, 0) for address in state["functions"]}
    skip = cached if args.retry_failed else attempted
    pending = [address for address in dict.fromkeys(selected) if address not in skip][:args.limit]
    if not pending:
        return {"cached": len(cached & set(selected)), "attempted": 0, "cleanup": "not-started"}
    run_dir = output / uuid.uuid4().hex
    run_dir.mkdir(mode=0o700)
    report = {"directory": run_dir.name, "selected": len(selected), "attempted": 0, "cleanup_errors": []}
    state["runs"].append(report)
    client = Client(command or default_command(args.rea), run_dir / "rea.stderr", args.timeout)
    opened = False
    failure = None
    try:
        initialized = client.request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                    "clientInfo": {"name": "paleblood-rea-drafts", "version": str(VERSION)}})
        client.notify("notifications/initialized", {})
        tools = client.request("tools/list", {})["result"]["tools"]
        if REQUIRED_TOOLS - {tool["name"] for tool in tools}:
            raise ValueError("REA server is missing required native/export operations")
        profile = {"server": initialized["result"]["serverInfo"],
                   "tools_sha256": hashlib.sha256(encoded(tools).encode()).hexdigest()}
        if state.get("server_profile", profile) != profile:
            raise ValueError("REA server/tool profile changed; choose a new output directory")
        state["server_profile"] = profile
        opened = True  # Even a failed open can retain provider resources requiring close.
        save(run_dir / "open.json", response := client.tool("open_binary", {"path": str(image), "provider_id": args.provider}))
        opened_result = evidence(response).get("result", {})
        if opened_result.get("sha256") != identity["image_sha256"]:
            raise ValueError("REA opened a different image")
        response = client.tool("list_procedures", {})
        save(run_dir / "procedures.json", response)
        inventory = evidence(response)
        analysis_profile = inventory.get("analysis_profile", {}).get("digest")
        if not analysis_profile:
            raise ValueError("REA inventory has no native analysis profile digest")
        if state.get("analysis_profile_digest", analysis_profile) != analysis_profile:
            raise ValueError("native analysis profile changed; choose a new output directory")
        state["analysis_profile_digest"] = analysis_profile
        procedures = set()
        for row in inventory["normalized_result"]:
            try:
                procedures.add(int(row["address"], 0))
            except ValueError:
                continue  # Named external address spaces cannot match file-backed catalog entries.
        for address in pending:
            key, native = f"0x{address:08x}", address - args.image_base + args.analysis_base
            row = {"analysis_address": f"0x{native:x}", "status": "missing-procedure"}
            report["attempted"] += 1
            state["functions"][key] = row
            if native in procedures:
                try:
                    info = client.tool("procedure_info", {"procedure": hex(native)})
                    save(run_dir / f"{key}-info.json", info)
                    if int(evidence(info)["normalized_result"]["entrypoint"], 0) != native:
                        raise ValueError("REA returned a neighboring or containing procedure")
                    mapping = client.tool("address_to_file_offset", {"address": hex(native)})
                    save(run_dir / f"{key}-mapping.json", mapping)
                    if evidence(mapping)["normalized_result"]["file_offset"] != file_offset(mappings, native - args.analysis_base):
                        raise ValueError("REA source-file mapping disagrees with ELF PT_LOAD")
                    response = client.tool("analyze_function", {"procedure": hex(native)})
                    path = run_dir / f"{key}.json"
                    save(path, response)
                    dossier = evidence(response)
                    if dossier.get("subject", {}).get("digest", {}).get("sha256") != identity["image_sha256"]:
                        raise ValueError("function dossier belongs to a different image")
                    if dossier.get("analysis_profile", {}).get("digest") != analysis_profile:
                        raise ValueError("function dossier native analysis profile changed")
                    normalized = dossier["normalized_result"]
                    if int(normalized["procedure"]["address"], 0) != native:
                        raise ValueError("function dossier belongs to a different procedure")
                    row.update(status="dossier", file=str(path.relative_to(output)), sha256=digest(path),
                               evidence_id=dossier["evidence_id"],
                               has_pseudocode=bool(normalized.get("pseudocode", "").strip()) and
                               normalized.get("facet_availability", {}).get("pseudocode", {}).get("available") is not False)
                except (RuntimeError, ValueError, KeyError) as error:
                    row.update(status="error", error=str(error))
                    if "resource_constraint" in str(error) and "transport" in str(error):
                        row["status"] = "retained-transport-limited"
                        raise RuntimeError("analysis exceeded transport limits; inspect the exported Evidence bundle") from error
                    if "cleanup_incomplete" in str(error):
                        raise
            save(state_path, state)
            print(f"{key}: {row['status']}", file=sys.stderr, flush=True)
    except Exception as error:
        failure = error
        report["error"] = str(error)
    finally:
        if opened and client.pending is None:
            for name, arguments in (("export_evidence_bundle", {"path": str(run_dir / "evidence.json")}),
                                    ("close_binary", {})):
                try:
                    response = client.tool(name, arguments)
                    save(run_dir / f"{name}.json", response)
                    evidence(response)
                except Exception as error:
                    report["cleanup_errors"].append(f"{name}: {error}")
                    if client.pending is not None:
                        break
        elif client.pending is not None:
            report["cleanup_errors"].append("REA request did not drain; provider/document cleanup is unconfirmed")
        client.stop()
        report["cleanup"] = "unconfirmed" if report["cleanup_errors"] else "closed" if opened else "not-started"
        save(state_path, state)
    if failure is not None:
        raise RuntimeError(str(failure)) from failure
    if report["cleanup_errors"]:
        raise RuntimeError("; ".join(report["cleanup_errors"]))
    report["statuses"] = dict(Counter(state["functions"][f"0x{address:08x}"]["status"] for address in pending))
    report["pseudocode"] = sum(bool(state["functions"][f"0x{address:08x}"].get("has_pseudocode")) for address in pending)
    save(state_path, state)
    return report


def default_command(rea: str | None) -> list[str]:
    if rea:
        return [rea, "--mcp"]
    installed = shutil.which("rea")
    return [installed, "--mcp"] if installed else ["npx", "-y", "rea-agents@6.7.0", "--mcp"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("addresses", nargs="*", help="catalog addresses; use --all for catalog order")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, help="rea_image.py provenance for a derived analysis image")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--provider", choices=("hopper", "ghidra", "ida", "kuna"), default="hopper")
    parser.add_argument("--image-base", type=lambda s: int(s, 0), default=0x400000,
                        help="fixed catalog origin (0x400000); use --analysis-base for a rebased provider")
    parser.add_argument("--analysis-base", type=lambda s: int(s, 0), default=0,
                        help="provider address assigned to ELF virtual address zero (default 0)")
    parser.add_argument("--all", action="store_true", help="select the catalog, still bounded by --limit")
    parser.add_argument("--limit", type=int, default=8, help="maximum uncached function attempts (default 8)")
    parser.add_argument("--timeout", type=float, default=600, help="per-request deadline in seconds (default 600)")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--retry-failed", action="store_true",
                        help="with --resume, retry missing/error entries instead of advancing the catalog sweep")
    parser.add_argument("--rea", help="installed public rea executable; defaults to rea or pinned npx")
    args = parser.parse_args()
    if args.limit < 1 or not 0 < args.timeout <= 3600 or args.analysis_base < 0:
        parser.error("limit must be positive, timeout in (0,3600], analysis base nonnegative")
    try:
        report = run(args)
        print(json.dumps(report, indent=2))
        return int(bool(report.get("statuses", {}).get("error")))
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"rea-drafts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
