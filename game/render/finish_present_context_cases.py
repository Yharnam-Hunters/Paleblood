#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: finish_present_context_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02568530"


def cases():
    for ignored in (0, 0x89ABCDEF12345678):
        c = basic(f"ignored_delegate_result_{ignored:x}", returns="void", size=0xB740)
        c["stubs"] = [
            {"address": "0x02ad3070", "argc": 3, "ret": hex(ignored), "argmem": [{"arg": 2, "size": 72}]},
            {"address": "0x02ab9850", "argc": 2, "ret": hex(ignored)},
            {"address": "0x02acd190", "argc": 2, "ret": hex(ignored)},
            {"address": "0x02aaf700", "argc": 1, "ret": hex(ignored)},
            {"address": "0x02abba10", "argc": 1, "ret": hex(ignored)},
            {"address": "0x02ad3300", "argc": 1, "ret": hex(ignored)},
            {"address": "0x02ad2d20", "argc": 1, "ret": hex(ignored)},
        ]
        yield c


def main():
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
