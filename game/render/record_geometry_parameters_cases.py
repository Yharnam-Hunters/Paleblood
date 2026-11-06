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

ADDRESS = "0x0149ed30"
RETURN = "i32"
INPUTS = [
    {"args": [0, 8, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 8, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 8, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 8, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 32, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 32, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 32, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 32, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 64, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 64, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 64, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 64, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 128, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 128, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 128, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 128, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 8, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 8, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 8, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 8, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 32, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 32, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 32, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 32, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 64, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 64, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 64, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 64, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 128, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 128, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 128, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [1, 128, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 8, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 8, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 8, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 8, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 32, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 32, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 32, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 32, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 64, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 64, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 64, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 64, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 128, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 128, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 128, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [4, 128, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 8, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 8, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 8, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 8, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 32, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 32, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 32, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 32, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 64, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 64, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 64, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 64, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 128, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 128, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 128, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [8, 128, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 8, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 8, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 8, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 8, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 32, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 32, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 32, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 32, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 64, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 64, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 64, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 64, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 128, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 128, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 128, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [16, 128, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 8, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 8, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 8, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 8, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 32, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 32, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 32, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 32, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 64, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 64, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 64, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 64, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 128, 1, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 128, 2, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 128, 4, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [31, 128, 8, "p33", "p40", "p44", "p48"], "words": []},
    {"args": [0, 64, 1, 0, "p33", "p34", "p33"], "words": []},
    {"args": [1, 64, 1, 0, "p33", "p34", "p33"], "words": []},
    {"args": [4, 64, 1, 0, "p33", "p34", "p33"], "words": []},
    {"args": [16, 64, 1, 0, "p33", "p34", "p33"], "words": []},
    {"args": [31, 64, 1, 0, "p33", "p34", "p33"], "words": []},
    {"args": [32, 64, 1, 0, "p33", "p34", "p33"], "words": []},
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
