#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: shader_address_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x01476390"


def cases():
    for stage in (0, 1, 2, 5):
        for value in (0, 1, 0xFFFFFFFF, 0x123456789ABCDEF0):
            c = shader_case(f"stage_{stage}_value_{value:016x}", stage, value, 80, 0)
            yield c
    for available, result in ((0, 0), (4 - 1, 0x100), (0, 1), (4, 0)):
        yield shader_case(f"capacity_{available}_ret_{result:x}", 2, 0x89ABCDEF01234567, available, result)

    c = basic("packet_overwrites_cursor", returns="void", size=512)
    c["buffers"]["descriptor"] = {"size": 64}
    c["args"].update({"rsi": 1, "rdx": 16, "rcx": "buf:descriptor"})
    c["memory"] = [{"addr": "buf:self+8", "pointer": "self+512"}, {"addr": "buf:self+16", "pointer": "self+8"}]
    yield c


def shader_case(cid, stage, value, available, result):
    c = basic(cid, returns="void", size=64)
    c["buffers"].update(
        {"packet": {"size": 512, "bytes": "6b" * 512}, "descriptor": {"size": 64, "bytes": bytes(range(64)).hex()}}
    )
    c["args"].update({"rsi": stage, "rdx": hex(value & 0xFFFFFFFF), "rcx": hex(value)})
    c["memory"] = [
        {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
        {"addr": "buf:self+16", "pointer": "packet"},
        {"addr": "buf:self+24", "guest": "0x014712b0"},
        {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
        memory(40, stage),
    ]
    if available < 4:
        c["stubs"] = [{"address": "0x014712b0", "argc": 3, "ret": hex(result)}]
    return c


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
