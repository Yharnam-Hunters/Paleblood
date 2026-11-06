#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: emit_present_context_section_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys

MASK64 = (1 << 64) - 1


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def memory(offset, value):
    return {"addr": f"buf:self+{offset}", "bytes": word(value)}


def basic(cid, returns="i64", size=512):
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": returns,
        "args": {"rdi": "buf:self"},
        "buffers": {"self": {"size": size, "bytes": "a5" * size}},
        "memory": [],
        "stubs": [],
    }


ADDRESS = "0x01472870"


def cases():
    for available in (0, 255, 256, 257, 0x100000100, 0x1000000FF):
        for result in (0, 1, 0x80000001, 0xDEADBEEF00000007):
            c = basic(f"available_{available:x}_result_{result:x}", size=64)
            c["buffers"]["packet"] = {"size": 2048, "bytes": "6b" * 2048}
            c["memory"] = [
                {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
                {"addr": "buf:self+16", "pointer": "packet"},
                {"addr": "buf:self+24", "guest": "0x014712b0"},
                {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
            ]
            if available & 0xFFFFFFFF < 256:
                c["stubs"].append({"address": "0x014712b0", "argc": 3, "ret": 1})
            c["stubs"].append({"address": "0x02fc04a8", "argc": 2, "ret": hex(result)})
            if available > 512:
                c["memory"][0] = {"addr": "buf:self+8", "bytes": qword(0x1000 + available * 4)}
                c["memory"][1] = {"addr": "buf:self+16", "bytes": qword(0x1000)}
            yield c
    c = basic("callback_failure_retains_high_bits", size=64)
    c["buffers"]["packet"] = {"size": 64}
    c["memory"] = [
        {"addr": "buf:self+8", "pointer": "packet"},
        {"addr": "buf:self+16", "pointer": "packet"},
        {"addr": "buf:self+24", "guest": "0x014712b0"},
        {"addr": "buf:self+32", "bytes": qword(7)},
    ]
    c["stubs"] = [{"address": "0x014712b0", "argc": 3, "ret": "0x89abcdef00000100"}]
    yield c

    for cid, refresh, import_refresh in (
        ("callback_refreshes_cursor_and_limit", True, False),
        ("import_changes_live_cursor", False, True),
        ("callback_and_import_change_cursor", True, True),
    ):
        c = basic(cid, size=64)
        c["buffers"].update(
            {"packet": {"size": 2048, "bytes": "6b" * 2048}, "other": {"size": 2048, "bytes": "74" * 2048}}
        )
        c["memory"] = [
            {"addr": "buf:self+8", "pointer": "packet" if refresh else "packet+1024"},
            {"addr": "buf:self+16", "pointer": "packet"},
            {"addr": "buf:self+24", "guest": "0x014712b0"},
            {"addr": "buf:self+32", "bytes": qword(7)},
        ]
        if refresh:
            c["stubs"].append(
                {
                    "address": "0x014712b0",
                    "argc": 3,
                    "ret": 1,
                    "writes": [
                        {"arg": 0, "offset": 8, "pointer": "other+1536", "size": 8},
                        {"arg": 0, "offset": 16, "pointer": "other+32", "size": 8},
                    ],
                }
            )
        call = {"address": "0x02fc04a8", "argc": 2, "ret": 7}
        if import_refresh:
            # The import destination aliases the command buffer; its stub writes
            # the command-buffer cursor through the destination argument.
            c["memory"][1] = {"addr": "buf:self+16", "pointer": "self"}
            if refresh:
                c["memory"][0] = {"addr": "buf:self+8", "pointer": "self"}
            if refresh:
                c["stubs"][0]["writes"][1]["pointer"] = "self"
            call["writes"] = [{"arg": 0, "offset": 16, "pointer": "packet+128", "size": 8}]
        c["stubs"].append(call)
        yield c


def main():
    """Write deterministic synthetic cases to the requested directory."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
