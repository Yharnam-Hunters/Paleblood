#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: set_vs_shader_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x01475480"


def cases():
    for available, callback in ((80, 0), (28, 0), (29, 0), (0, 0), (0, 1)):
        for result in (0, 1, 0x100, 0xFFFFFFFF, 0x89ABCDEF00000000):
            c = basic(f"available_{available}_callback_{callback}_result_{result:x}", returns="void", size=64)
            c["buffers"].update(
                {"packet": {"size": 512, "bytes": "6b" * 512}, "descriptor": {"size": 64, "bytes": "7c" * 64}}
            )
            c["args"].update({"rsi": "buf:descriptor", "rdx": "0xabcdef1234567890"})
            c["memory"] = [
                {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
                {"addr": "buf:self+16", "pointer": "packet"},
                {"addr": "buf:self+24", "guest": "0x014712b0"},
                {"addr": "buf:self+32", "bytes": qword(7)},
            ]
            if available < 29:
                c["stubs"].append({"address": "0x014712b0", "argc": 3, "ret": callback})
            if available >= 29 or callback:
                c["stubs"].append(
                    {
                        "address": "0x02fc04d8",
                        "argc": 4,
                        "ret": hex(result),
                        "writes": [{"arg": 0, "bytes": "55aa33cc", "size": 4}],
                    }
                )
            yield c

    c = basic("writer_changes_live_cursor", returns="void", size=512)
    c["buffers"].update({"descriptor": {"size": 64}, "other": {"size": 512}})
    c["args"].update({"rsi": "buf:descriptor", "rdx": 0})
    c["memory"] = [{"addr": "buf:self+8", "pointer": "self+512"}, {"addr": "buf:self+16", "pointer": "self"}]
    c["stubs"] = [
        {
            "address": "0x02fc04d8",
            "argc": 4,
            "ret": 0,
            "writes": [{"arg": 0, "offset": 16, "pointer": "other+32", "size": 8}],
        }
    ]
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
