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

ADDRESS = "0x0149c660"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", 0, 0, 0], "words": []},
    {"args": ["p33", 0, 1, 4], "words": []},
    {"args": ["p33", 0, 8, 2], "words": []},
    {"args": ["p33", 0, 32, 8], "words": []},
    {"args": ["p33", 0, 64, 1], "words": []},
    {"args": ["p33", 0, 64, 16], "words": []},
    {"args": ["p33", 0, 128, 2], "words": []},
    {"args": ["p33", 0, 129, 1], "words": []},
    {"args": ["p33", 0, 129, 16], "words": []},
    {"args": ["p33", 1, 0, 4], "words": []},
    {"args": ["p33", 1, 1, 8], "words": []},
    {"args": ["p33", 1, 8, 1], "words": []},
    {"args": ["p33", 1, 8, 16], "words": []},
    {"args": ["p33", 1, 32, 2], "words": []},
    {"args": ["p33", 1, 64, 0], "words": []},
    {"args": ["p33", 1, 128, 1], "words": []},
    {"args": ["p33", 1, 128, 16], "words": []},
    {"args": ["p33", 1, 129, 0], "words": []},
    {"args": ["p33", 2, 0, 8], "words": []},
    {"args": ["p33", 2, 1, 2], "words": []},
    {"args": ["p33", 2, 8, 0], "words": []},
    {"args": ["p33", 2, 32, 1], "words": []},
    {"args": ["p33", 2, 32, 16], "words": []},
    {"args": ["p33", 2, 64, 4], "words": []},
    {"args": ["p33", 2, 128, 0], "words": []},
    {"args": ["p33", 2, 129, 4], "words": []},
    {"args": ["p33", 4, 0, 1], "words": []},
    {"args": ["p33", 4, 0, 16], "words": []},
    {"args": ["p33", 4, 1, 0], "words": []},
    {"args": ["p33", 4, 8, 8], "words": []},
    {"args": ["p33", 4, 32, 4], "words": []},
    {"args": ["p33", 4, 64, 2], "words": []},
    {"args": ["p33", 4, 128, 8], "words": []},
    {"args": ["p33", 4, 129, 2], "words": []},
    {"args": ["p33", 8, 0, 2], "words": []},
    {"args": ["p33", 8, 1, 1], "words": []},
    {"args": ["p33", 8, 1, 16], "words": []},
    {"args": ["p33", 8, 8, 4], "words": []},
    {"args": ["p33", 8, 32, 0], "words": []},
    {"args": ["p33", 8, 64, 8], "words": []},
    {"args": ["p33", 8, 128, 4], "words": []},
    {"args": ["p33", 8, 129, 8], "words": []},
    {"args": ["p33", 16, 0, 4], "words": []},
    {"args": ["p33", 16, 1, 8], "words": []},
    {"args": ["p33", 16, 8, 1], "words": []},
    {"args": ["p33", 16, 8, 16], "words": []},
    {"args": ["p33", 16, 32, 2], "words": []},
    {"args": ["p33", 16, 64, 0], "words": []},
    {"args": ["p33", 16, 128, 1], "words": []},
    {"args": ["p33", 16, 128, 16], "words": []},
    {"args": ["p33", 16, 129, 0], "words": []},
    {"args": ["p33", 31, 0, 4], "words": []},
    {"args": ["p33", 31, 1, 8], "words": []},
    {"args": ["p33", 31, 8, 1], "words": []},
    {"args": ["p33", 31, 8, 16], "words": []},
    {"args": ["p33", 31, 32, 2], "words": []},
    {"args": ["p33", 31, 64, 0], "words": []},
    {"args": ["p33", 31, 128, 1], "words": []},
    {"args": ["p33", 31, 128, 16], "words": []},
    {"args": ["p33", 31, 129, 0], "words": []},
    {"args": ["p33", 32, 0, 8], "words": []},
    {"args": ["p33", 32, 1, 2], "words": []},
    {"args": ["p33", 32, 8, 0], "words": []},
    {"args": ["p33", 32, 32, 1], "words": []},
    {"args": ["p33", 32, 32, 16], "words": []},
    {"args": ["p33", 32, 64, 4], "words": []},
    {"args": ["p33", 32, 128, 0], "words": []},
    {"args": ["p33", 32, 129, 4], "words": []},
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
