#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: reset_present_context_shader_state_cases.py OUT_DIR. Generate captures outside the repository.
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


def basic(cid, returns="void", size=512):
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


ADDRESS = "0x02acd190"


def cases():
    for active in (0, 1, 0xFFFFFFFF):
        for result in (0, 0x89ABCDEF12345678):
            c = basic(f"active_{active:08x}_result_{result:016x}", size=0xB2C0)
            c["buffers"]["context"] = {"size": 64, "bytes": "a7" * 64}
            c["args"]["rsi"] = "buf:context"
            c["memory"] = [memory(4, active)]
            c["stubs"] = [{"address": "0x02abb6c0", "argc": 2, "ret": hex(result)}]
            for size in [0x610] * 6 + [0x108, 0x20E, 0xE00]:
                c["stubs"].append(
                    {
                        "address": "0x02fbe688",
                        "argc": 3,
                        "ret": hex(result),
                        "writes": [{"arg": 0, "bytes": "00" * size, "size": size}],
                    }
                )
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
