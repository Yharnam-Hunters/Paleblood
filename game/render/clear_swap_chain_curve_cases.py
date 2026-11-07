#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: clear_swap_chain_curve_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02571c60"


def cases():
    for stage in ("null_owner", "null_resource", "callback", "callback_replaces_fields"):
        c = basic(stage, returns="void", size=32)
        c["buffers"].update({"owner": {"size": 48}, "vtable": {"size": 128}})
        c["memory"] = [
            (
                {"addr": "buf:self", "bytes": qword(0)}
                if stage == "null_owner"
                else {"addr": "buf:self", "pointer": "owner"}
            ),
            {"addr": "buf:self+8", "bytes": qword(0 if stage == "null_resource" else 0x123456789ABCDEF0)},
            {"addr": "buf:owner", "pointer": "vtable"},
            {"addr": "buf:vtable+112", "guest": "0x014712b0"},
        ]
        c["stubs"] = [{"address": "0x014712b0", "argc": 2, "ret": "0xfedcba9876543210"}]
        if stage == "callback_replaces_fields":
            # Use the same allocation for owner and record so a callback can
            # replace both fields without reaching outside its declared object.
            c["args"]["rdi"] = "buf:owner+16"
            c["memory"].extend(
                [
                    {"addr": "buf:owner+16", "pointer": "owner"},
                    {"addr": "buf:owner+24", "bytes": qword(0x1122334455667788)},
                ]
            )
            c["stubs"][0]["writes"] = [
                {"arg": 0, "offset": 16, "bytes": qword(0x9876543210) + qword(0x55555555), "size": 16},
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
