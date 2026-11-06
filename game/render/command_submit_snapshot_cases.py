#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: command_submit_snapshot_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ab0e30"


def cases():
    for size in (0, 1, 3, 4, 7, 32):
        for handle in (0, 0x80000001, 0xFFFFFFFF):
            c = basic(f"bytes_{size}_handle_{handle:x}", returns="void", size=64)
            c["buffers"].update({"snapshot": {"size": 64}, "commands": {"size": 128}})
            c["args"]["rsi"] = "buf:snapshot"
            c["memory"] = [
                memory(0, handle),
                memory(4, 0x89ABCDEF),
                memory(8, 3),
                {"addr": "buf:self+16", "bytes": qword(0x123456789ABCDEF0)},
                {"addr": "buf:snapshot", "pointer": "commands"},
                {"addr": "buf:snapshot+16", "pointer": f"commands+{size}"},
            ]
            c["stubs"] = [
                {"address": "0x01471270", "argc": 5, "ret": 0, "writes": [{"arg": 0, "bytes": "00" * 24, "size": 24}]}
            ]
            if size & ~3:
                c["stubs"] += [
                    {
                        "address": "0x01489a20",
                        "argc": 9,
                        "ret": "0x80020016",
                        "argmem": [{"arg": 1, "size": 8}, {"arg": 2, "size": 4}],
                    },
                    {"address": "0x01489b50", "argc": 0, "ret": 7},
                ]
            yield c


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    # Capture defined low bits of integer parameters, excluding ABI padding.
    for entry in generated:
        for callback in entry.get("stubs", []):
            address = callback["address"]
            if address == "0x01489a20":
                callback.update(argi=[1, 2, 3, 4, 5, 8], argu32=[0, 6, 7])
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
