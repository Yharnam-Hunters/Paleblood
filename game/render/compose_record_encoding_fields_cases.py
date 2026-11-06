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

ADDRESS = "0x014712d0"
RETURN = "i32"
INPUTS = [
    {"args": [0, 0, 57, 1, 147, 4294967295], "words": []},
    {"args": [0, 1, 56, 2, 146, 0], "words": []},
    {"args": [0, 7, 62, 8, 148, 6], "words": []},
    {"args": [0, 15, 54, 16, 156, 14], "words": []},
    {"args": [0, 16, 41, 17, 131, 15], "words": []},
    {"args": [0, 4294967295, 4294967238, 4294967296, 4294967148, 4294967294], "words": []},
    {"args": [1, 0, 57, 1, 147, 4294967295], "words": []},
    {"args": [1, 1, 56, 2, 146, 0], "words": []},
    {"args": [1, 7, 62, 8, 148, 6], "words": []},
    {"args": [1, 15, 54, 16, 156, 14], "words": []},
    {"args": [1, 16, 41, 17, 131, 15], "words": []},
    {"args": [1, 4294967295, 4294967238, 4294967296, 4294967148, 4294967294], "words": []},
    {"args": [255, 0, 57, 1, 147, 4294967295], "words": []},
    {"args": [255, 1, 56, 2, 146, 0], "words": []},
    {"args": [255, 7, 62, 8, 148, 6], "words": []},
    {"args": [255, 15, 54, 16, 156, 14], "words": []},
    {"args": [255, 16, 41, 17, 131, 15], "words": []},
    {"args": [255, 4294967295, 4294967238, 4294967296, 4294967148, 4294967294], "words": []},
    {"args": [256, 0, 57, 1, 147, 4294967295], "words": []},
    {"args": [256, 1, 56, 2, 146, 0], "words": []},
    {"args": [256, 7, 62, 8, 148, 6], "words": []},
    {"args": [256, 15, 54, 16, 156, 14], "words": []},
    {"args": [256, 16, 41, 17, 131, 15], "words": []},
    {"args": [256, 4294967295, 4294967238, 4294967296, 4294967148, 4294967294], "words": []},
    {"args": [4294967295, 0, 57, 1, 147, 4294967295], "words": []},
    {"args": [4294967295, 1, 56, 2, 146, 0], "words": []},
    {"args": [4294967295, 7, 62, 8, 148, 6], "words": []},
    {"args": [4294967295, 15, 54, 16, 156, 14], "words": []},
    {"args": [4294967295, 16, 41, 17, 131, 15], "words": []},
    {"args": [4294967295, 4294967295, 4294967238, 4294967296, 4294967148, 4294967294], "words": []},
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
