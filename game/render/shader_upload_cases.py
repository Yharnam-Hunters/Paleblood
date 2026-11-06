#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: shader_upload_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02aaddc0"


def cases():
    for size in (0, 1, 4, 32768, 32769, 65535, 65536, 65537, 0xFFFFFFFFFFFFFFFF):
        for existing in (False, True):
            yield upload_case(f"bytes_{size:x}_existing_{int(existing)}", size, existing, False)
    yield upload_case("acquire_reclaim_then_success", 16, False, True)
    c = upload_case("both_acquisitions_fail", 16, False, True)
    c["stubs"][-1]["ret"] = 0
    yield c

    for index, size, used in ((4, 8, 65520), (8, 128, 65280), (10, 512, 64000), (15, 16, 65520), (15, 32, 65500)):
        for upper in (0, 0x1234567800000000):
            for linked in (False, True):
                c = upload_case(
                    f"bin_{index}_size_{size}_used_{used}_upper_{upper:x}_next_{int(linked)}", size, False, False
                )
                c["stubs"] = []
                c["buffers"].update(
                    {"next": {"size": 64, "bytes": "39" * 64}, "full": {"size": 64, "bytes": "48" * 64}}
                )
                c["memory"] += [
                    {"addr": f"buf:self+{8+8*index}", "pointer": "node"},
                    memory(144, 1 << index),
                    {"addr": "buf:node", "bytes": qword(upper | used)},
                    {"addr": "buf:self+136", "pointer": "full"},
                ]
                if linked:
                    c["memory"].append({"addr": "buf:node+40", "pointer": "next"})
                yield c


def upload_case(cid, size, existing, retry):
    c = basic(cid, size=256)
    c["buffers"]["self"]["bytes"] = "00" * 256
    c["buffers"].update(
        {"node": {"size": 64}, "data": {"size": 65536}, "manager": {"size": 16}, "commands": {"size": 16}}
    )
    c["args"].update({"rsi": "buf:commands", "rdx": hex(size)})
    c["memory"] = [{"addr": "buf:self", "pointer": "manager"}, {"addr": "buf:node+8", "pointer": "data"}]
    eligible = existing and size <= 32768
    if eligible:
        c["memory"] += [{"addr": "buf:self+128", "pointer": "node"}, memory(144, 1 << 15)]
    if size <= 65536 and not eligible:
        c["stubs"] = [{"address": "0x02aa9d50", "argc": 2, "ret": 0 if retry else "buf:node"}]
        if retry:
            c["stubs"] += [
                {"address": "0x02aaa860", "argc": 2, "ret": 0},
                {"address": "0x02aa9d50", "argc": 2, "ret": "buf:node"},
            ]
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
