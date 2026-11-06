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

ADDRESS = "0x0149c4e0"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", 0, 0], "words": []},
    {"args": ["p33", 0, 1], "words": []},
    {"args": ["p33", 0, 2], "words": []},
    {"args": ["p33", 0, 4294967295], "words": []},
    {"args": ["p33", 1, 0], "words": []},
    {"args": ["p33", 1, 1], "words": []},
    {"args": ["p33", 1, 2], "words": []},
    {"args": ["p33", 1, 4294967295], "words": []},
    {"args": ["p33", 2, 0], "words": []},
    {"args": ["p33", 2, 1], "words": []},
    {"args": ["p33", 2, 2], "words": []},
    {"args": ["p33", 2, 4294967295], "words": []},
    {"args": ["p33", 3, 0], "words": []},
    {"args": ["p33", 3, 1], "words": []},
    {"args": ["p33", 3, 2], "words": []},
    {"args": ["p33", 3, 4294967295], "words": []},
    {"args": ["p33", 4, 0], "words": []},
    {"args": ["p33", 4, 1], "words": []},
    {"args": ["p33", 4, 2], "words": []},
    {"args": ["p33", 4, 4294967295], "words": []},
    {"args": ["p33", 5, 0], "words": []},
    {"args": ["p33", 5, 1], "words": []},
    {"args": ["p33", 5, 2], "words": []},
    {"args": ["p33", 5, 4294967295], "words": []},
    {"args": ["p33", 6, 0], "words": []},
    {"args": ["p33", 6, 1], "words": []},
    {"args": ["p33", 6, 2], "words": []},
    {"args": ["p33", 6, 4294967295], "words": []},
    {"args": ["p33", 7, 0], "words": []},
    {"args": ["p33", 7, 1], "words": []},
    {"args": ["p33", 7, 2], "words": []},
    {"args": ["p33", 7, 4294967295], "words": []},
    {"args": ["p33", 8, 0], "words": []},
    {"args": ["p33", 8, 1], "words": []},
    {"args": ["p33", 8, 2], "words": []},
    {"args": ["p33", 8, 4294967295], "words": []},
    {"args": ["p33", 9, 0], "words": []},
    {"args": ["p33", 9, 1], "words": []},
    {"args": ["p33", 9, 2], "words": []},
    {"args": ["p33", 9, 4294967295], "words": []},
    {"args": ["p33", 10, 0], "words": []},
    {"args": ["p33", 10, 1], "words": []},
    {"args": ["p33", 10, 2], "words": []},
    {"args": ["p33", 10, 4294967295], "words": []},
    {"args": ["p33", 11, 0], "words": []},
    {"args": ["p33", 11, 1], "words": []},
    {"args": ["p33", 11, 2], "words": []},
    {"args": ["p33", 11, 4294967295], "words": []},
    {"args": ["p33", 12, 0], "words": []},
    {"args": ["p33", 12, 1], "words": []},
    {"args": ["p33", 12, 2], "words": []},
    {"args": ["p33", 12, 4294967295], "words": []},
    {"args": ["p33", 13, 0], "words": []},
    {"args": ["p33", 13, 1], "words": []},
    {"args": ["p33", 13, 2], "words": []},
    {"args": ["p33", 13, 4294967295], "words": []},
    {"args": ["p33", 14, 0], "words": []},
    {"args": ["p33", 14, 1], "words": []},
    {"args": ["p33", 14, 2], "words": []},
    {"args": ["p33", 14, 4294967295], "words": []},
    {"args": ["p33", 15, 0], "words": []},
    {"args": ["p33", 15, 1], "words": []},
    {"args": ["p33", 15, 2], "words": []},
    {"args": ["p33", 15, 4294967295], "words": []},
]


# The whole parameter word must equal1; a matching low byte is insufficient.
INPUTS.extend({"args": ["p33", selector, 0x101], "words": []} for selector in range(7, 13))


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
