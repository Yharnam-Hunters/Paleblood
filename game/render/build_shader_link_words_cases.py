#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: build_shader_link_words_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x014873a0"


def cases():
    for count in (0, 1, 2, 4):
        for first_count in (0, 1, 4):
            for alias in (False, True):
                c = basic(f"count_{count}_first_{first_count}_alias_{int(alias)}", returns="void", size=64)
                c["buffers"]["first"] = {"size": 32, "bytes": "01020214036504ff" + "55" * 24}
                c["buffers"]["second"] = {"size": 32, "bytes": "010302140f6504ff" + "66" * 24}
                c["args"].update({"rsi": "buf:first", "rdx": first_count, "rcx": "buf:second", "r8": count})
                if alias:
                    c["args"]["rdi"] = "buf:second"
                yield c

    for cid, odd in (("duplicate_keys_first_match", False), ("odd_offset_live_rereads", True)):
        c = basic(cid, returns="void", size=128)
        c["buffers"]["first"] = {"size": 32, "bytes": "0101011e000304ff" + "55" * 24}
        c["buffers"]["second"] = {"size": 64, "bytes": "00" + "011f0214036504ff" + "66" * 55}
        c["args"].update({"rsi": "buf:first", "rdx": 3, "rcx": "buf:second+1", "r8": 4 if odd else 1})
        if odd:
            c["args"]["rdi"] = "buf:second"
        yield c

    for source in ("first", "second"):
        for flag in (4, 16):
            c = basic(f"output_{source}_plus_one_flag_{flag}", returns="void", size=128)
            c["buffers"]["first"] = {"size": 64, "bytes": bytes((1, flag, 2, flag, 3, flag, 4, flag)).hex() + "55" * 56}
            c["buffers"]["second"] = {
                "size": 64,
                "bytes": bytes((1, flag, 2, flag, 3, flag, 4, flag)).hex() + "66" * 56,
            }
            c["args"].update({"rdi": f"buf:{source}+1", "rsi": "buf:first", "rdx": 4, "rcx": "buf:second", "r8": 4})
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
