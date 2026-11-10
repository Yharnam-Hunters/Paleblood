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

ADDRESS = "0x0149e8d0"
RETURN = "i32"
INPUTS = [
    {"args": [0], "words": []},
    {"args": [1], "words": []},
    {"args": [2], "words": []},
    {"args": [3], "words": []},
    {"args": [4], "words": []},
    {"args": [7], "words": []},
    {"args": [8], "words": []},
    {"args": [15], "words": []},
    {"args": [16], "words": []},
    {"args": [31], "words": []},
    {"args": [32], "words": []},
    {"args": [63], "words": []},
    {"args": [64], "words": []},
    {"args": [127], "words": []},
    {"args": [128], "words": []},
    {"args": [255], "words": []},
    {"args": [256], "words": []},
    {"args": [2147483647], "words": []},
    {"args": [2147483648], "words": []},
    {"args": [4294967295], "words": []},
    {"args": [511], "words": []},
    {"args": [4095], "words": []},
    {"args": [32767], "words": []},
    {"args": [262143], "words": []},
    {"args": [2097151], "words": []},
    {"args": [16777215], "words": []},
    {"args": [134217727], "words": []},
    {"args": [1073741823], "words": []},
    {"args": [513], "words": []},
    {"args": [4097], "words": []},
    {"args": [32769], "words": []},
    {"args": [262145], "words": []},
    {"args": [2097153], "words": []},
    {"args": [16777217], "words": []},
    {"args": [134217729], "words": []},
    {"args": [1073741825], "words": []},
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
