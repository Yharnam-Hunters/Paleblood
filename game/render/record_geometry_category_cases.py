#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic cases: python3 this_script.py OUTPUT_DIRECTORY.

No expected values or game recordings.

Cases vary numeric admissions, masking boundaries and caller-owned canaries.
Snapshot comparisons do not prove race/fault behavior or restored temporary writes.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x0149ea90"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", 0], "words": []},
    {"args": [0, 0], "words": []},
    {"args": ["p33", 1], "words": []},
    {"args": [0, 1], "words": []},
    {"args": ["p33", 2], "words": []},
    {"args": [0, 2], "words": []},
    {"args": ["p33", 3], "words": []},
    {"args": [0, 3], "words": []},
    {"args": ["p33", 4], "words": []},
    {"args": [0, 4], "words": []},
    {"args": ["p33", 5], "words": []},
    {"args": [0, 5], "words": []},
    {"args": ["p33", 6], "words": []},
    {"args": [0, 6], "words": []},
    {"args": ["p33", 7], "words": []},
    {"args": [0, 7], "words": []},
    {"args": ["p33", 8], "words": []},
    {"args": [0, 8], "words": []},
    {"args": ["p33", 9], "words": []},
    {"args": [0, 9], "words": []},
    {"args": ["p33", 10], "words": []},
    {"args": [0, 10], "words": []},
    {"args": ["p33", 11], "words": []},
    {"args": [0, 11], "words": []},
    {"args": ["p33", 12], "words": []},
    {"args": [0, 12], "words": []},
    {"args": ["p33", 13], "words": []},
    {"args": [0, 13], "words": []},
    {"args": ["p33", 14], "words": []},
    {"args": [0, 14], "words": []},
    {"args": ["p33", 15], "words": []},
    {"args": [0, 15], "words": []},
    {"args": ["p33", 16], "words": []},
    {"args": [0, 16], "words": []},
    {"args": ["p33", 17], "words": []},
    {"args": [0, 17], "words": []},
    {"args": ["p33", 18], "words": []},
    {"args": [0, 18], "words": []},
    {"args": ["p33", 19], "words": []},
    {"args": [0, 19], "words": []},
    {"args": ["p33", 20], "words": []},
    {"args": [0, 20], "words": []},
    {"args": ["p33", 21], "words": []},
    {"args": [0, 21], "words": []},
    {"args": ["p33", 22], "words": []},
    {"args": [0, 22], "words": []},
    {"args": ["p33", 23], "words": []},
    {"args": [0, 23], "words": []},
    {"args": ["p33", 24], "words": []},
    {"args": [0, 24], "words": []},
    {"args": ["p33", 25], "words": []},
    {"args": [0, 25], "words": []},
    {"args": ["p33", 26], "words": []},
    {"args": [0, 26], "words": []},
    {"args": ["p33", 27], "words": []},
    {"args": [0, 27], "words": []},
    {"args": ["p33", 28], "words": []},
    {"args": [0, 28], "words": []},
    {"args": ["p33", 29], "words": []},
    {"args": [0, 29], "words": []},
    {"args": ["p33", 30], "words": []},
    {"args": [0, 30], "words": []},
    {"args": ["p33", 31], "words": []},
    {"args": [0, 31], "words": []},
    {"args": ["p33", 32], "words": []},
    {"args": [0, 32], "words": []},
    {"args": ["p33", 64], "words": []},
    {"args": [0, 64], "words": []},
    {"args": ["p33", 255], "words": []},
    {"args": [0, 255], "words": []},
    {"args": ["p33", 4294967295], "words": []},
    {"args": [0, 4294967295], "words": []},
]


def fixtures():
    """Yield synthetic inputs without calculating expected outputs."""
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
            "memory": ([{"addr": "0x057e3be0", "pointer": "arena+0x1c0"}] if inputs.get("guard") else []),
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
