#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: process_present_finish_item_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ad3c80"


def cases():
    for count in (0, 1, 2, 8):
        for result in (0, 0x89ABCDEF12345678):
            c = basic(f"count_{count}_result_{result:x}", size=64)
            c["buffers"].update({"commands": {"size": 64}, "item": {"size": 128, "bytes": "a5" * 128}})
            c["args"].update({"rsi": "buf:commands", "rdx": "buf:item+8"})
            c["memory"] = [{"addr": "buf:self+8", "bytes": qword(count)}]
            c["stubs"] = [{"address": "0x02abb6d0", "argc": 5, "argi": [0, 2, 3], "argu32": [1, 4], "ret": hex(result)}]
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
