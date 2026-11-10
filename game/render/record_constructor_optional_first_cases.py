#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic constructor arithmetic, selector and publication boundaries.

No game recordings or race/fault equivalence are claimed.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x0149e2d0"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", "p44", "p48", "p52", 0, 1, 3, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 0, 1, 4294967295, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 0, 65, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 0, 513, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 1, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 65, 0, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 65, 1, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 65, 3, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 65, 4294967295, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 513, 0, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 513, 1, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 513, 3, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 1, 513, 4294967295, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 63, 1, 3, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 63, 1, 4294967295, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 63, 65, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 63, 513, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 1, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 65, 0, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 65, 1, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 65, 3, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 65, 4294967295, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 513, 0, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 513, 1, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 513, 3, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 64, 513, 4294967295, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 1, 0, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 1, 1, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 1, 3, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 1, 4294967295, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 65, 0, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 65, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 65, 4294967295, 0], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 513, 0, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 65, 513, 4294967295, 0], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 511, 1, 3, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 511, 1, 4294967295, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 511, 65, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 511, 513, 1, 256], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 4294967295, 1, 0, 255], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 4294967295, 1, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 4294967295, 1, 4294967295, 0], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 4294967295, 65, 1, 1], "words": [], "guard": True},
    {"args": ["p33", "p44", "p48", "p52", 4294967295, 513, 1, 1], "words": [], "guard": True},
    {"args": [0, 0, 0, 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", 0, 0, 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, "p39", 0, 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p39", 0, 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, 0, "p45", 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", 0, "p45", 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, "p39", "p45", 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p39", "p45", 0, 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, 0, 0, "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", 0, 0, "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, "p39", 0, "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p39", 0, "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, 0, "p45", "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", 0, "p45", "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": [0, "p39", "p45", "p51", 65, 513, 3, 0], "words": [], "guard": True},
    {"args": ["p33", "p39", "p45", "p51", 65, 513, 3, 0], "words": [], "guard": True},
]

INPUTS.extend(
    [{"id": "quantity_mask_bit54", "args": ["p33", "p44", "p48", "p52", 1073741824, 2147483648, 1, 1], "words": []}]
)


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
            "id": inputs.get("id", "edge_" + format(index, "04d")),
            "returns": inputs.get("returns", RETURN),
            "args": args,
            "stack": values[6:],
            "buffers": {"arena": {"size": len(arena), "bytes": arena.hex()}},
            "memory": [],
            "imports": [],
            "stubs": inputs.get(
                "stubs", ([{"address": "0x02abb4a0", "argc": 1, "ret": "0x0"}] if inputs.get("report_stub") else [])
            ),
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
