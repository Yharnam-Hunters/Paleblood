#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored synthetic inputs. No expected values or game recordings.

Cases vary numeric admissions, masking boundaries and caller-owned canaries.
Snapshot comparisons do not prove race/fault behavior or restored temporary writes.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x01488370"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", 0], "words": [(45, 0)]},
    {"args": ["p33", 0], "words": [(45, 4294967295)]},
    {"args": ["p33", 0], "words": [(45, 2478601521)]},
    {"args": ["p33", 1], "words": [(45, 0)]},
    {"args": ["p33", 1], "words": [(45, 4294967295)]},
    {"args": ["p33", 1], "words": [(45, 2478601521)]},
    {"args": ["p33", 2], "words": [(45, 0)]},
    {"args": ["p33", 2], "words": [(45, 4294967295)]},
    {"args": ["p33", 2], "words": [(45, 2478601521)]},
    {"args": ["p33", 3], "words": [(45, 0)]},
    {"args": ["p33", 3], "words": [(45, 4294967295)]},
    {"args": ["p33", 3], "words": [(45, 2478601521)]},
    {"args": ["p33", 4], "words": [(45, 0)]},
    {"args": ["p33", 4], "words": [(45, 4294967295)]},
    {"args": ["p33", 4], "words": [(45, 2478601521)]},
    {"args": ["p33", 7], "words": [(45, 0)]},
    {"args": ["p33", 7], "words": [(45, 4294967295)]},
    {"args": ["p33", 7], "words": [(45, 2478601521)]},
    {"args": ["p33", 8], "words": [(45, 0)]},
    {"args": ["p33", 8], "words": [(45, 4294967295)]},
    {"args": ["p33", 8], "words": [(45, 2478601521)]},
    {"args": ["p33", 15], "words": [(45, 0)]},
    {"args": ["p33", 15], "words": [(45, 4294967295)]},
    {"args": ["p33", 15], "words": [(45, 2478601521)]},
    {"args": ["p33", 16], "words": [(45, 0)]},
    {"args": ["p33", 16], "words": [(45, 4294967295)]},
    {"args": ["p33", 16], "words": [(45, 2478601521)]},
    {"args": ["p33", 31], "words": [(45, 0)]},
    {"args": ["p33", 31], "words": [(45, 4294967295)]},
    {"args": ["p33", 31], "words": [(45, 2478601521)]},
    {"args": ["p33", 32], "words": [(45, 0)]},
    {"args": ["p33", 32], "words": [(45, 4294967295)]},
    {"args": ["p33", 32], "words": [(45, 2478601521)]},
    {"args": ["p33", 63], "words": [(45, 0)]},
    {"args": ["p33", 63], "words": [(45, 4294967295)]},
    {"args": ["p33", 63], "words": [(45, 2478601521)]},
    {"args": ["p33", 64], "words": [(45, 0)]},
    {"args": ["p33", 64], "words": [(45, 4294967295)]},
    {"args": ["p33", 64], "words": [(45, 2478601521)]},
    {"args": ["p33", 127], "words": [(45, 0)]},
    {"args": ["p33", 127], "words": [(45, 4294967295)]},
    {"args": ["p33", 127], "words": [(45, 2478601521)]},
    {"args": ["p33", 128], "words": [(45, 0)]},
    {"args": ["p33", 128], "words": [(45, 4294967295)]},
    {"args": ["p33", 128], "words": [(45, 2478601521)]},
    {"args": ["p33", 255], "words": [(45, 0)]},
    {"args": ["p33", 255], "words": [(45, 4294967295)]},
    {"args": ["p33", 255], "words": [(45, 2478601521)]},
    {"args": ["p33", 256], "words": [(45, 0)]},
    {"args": ["p33", 256], "words": [(45, 4294967295)]},
    {"args": ["p33", 256], "words": [(45, 2478601521)]},
    {"args": ["p33", 2147483647], "words": [(45, 0)]},
    {"args": ["p33", 2147483647], "words": [(45, 4294967295)]},
    {"args": ["p33", 2147483647], "words": [(45, 2478601521)]},
    {"args": ["p33", 2147483648], "words": [(45, 0)]},
    {"args": ["p33", 2147483648], "words": [(45, 4294967295)]},
    {"args": ["p33", 2147483648], "words": [(45, 2478601521)]},
    {"args": ["p33", 4294967295], "words": [(45, 0)]},
    {"args": ["p33", 4294967295], "words": [(45, 4294967295)]},
    {"args": ["p33", 4294967295], "words": [(45, 2478601521)]},
]


def fixtures():
    """Yield inputs without calculating the expected replacement outputs."""
    registers = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"]
    for index, inputs in enumerate(INPUTS):
        arena = bytearray((0x35 + 29 * i) & 255 for i in range(512))
        for offset, value in inputs.get("words", []):
            struct.pack_into("<I", arena, offset, value & 0xFFFFFFFF)

        def wire(value):
            return (
                "buf:arena+0x" + format(int(value[1:]), "x")
                if isinstance(value, str) and value.startswith("p")
                else hex(value)
            )

        values = [wire(v) for v in inputs["args"]]
        args = {register: (values[i] if i < len(values) else "0x0") for i, register in enumerate(registers)}
        args.update({k: wire(v) for k, v in inputs.get("incoming", {}).items()})
        case = {
            "schema": 1,
            "address": ADDRESS,
            "id": "edge_" + format(index, "04d"),
            "returns": inputs.get("returns", RETURN),
            "args": args,
            "stack": values[6:],
            "buffers": {"arena": {"size": len(arena), "bytes": arena.hex()}},
            "memory": [],
            "imports": [],
            "stubs": ([{"address": "0x02abb4a0", "argc": 1, "ret": "0x0"}] if inputs.get("report_stub") else []),
        }
        yield case


def main():
    """Write deterministic synthetic cases to the requested directory."""
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    cases = list(fixtures())
    for case in cases:
        (destination / (case["id"] + ".json")).write_text(json.dumps(case, indent=1) + "\n")
    print(str(len(cases)) + " synthetic cases written")


if __name__ == "__main__":
    main()
