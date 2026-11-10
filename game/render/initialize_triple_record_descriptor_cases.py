#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic triple descriptor count admission with a strictly largest third component.

Authored synthetic cases only; no genuine game recordings.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x01488590"
RETURN = "i64"
INPUTS = [
    {"args": ["p32", 0, 8, 4, 0, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 1, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 2, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 3, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 0, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 1, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 2, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 3, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 0, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 1, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 2, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 3, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 0, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 1, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 2, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 3, 16433153, 0], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 0, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 1, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 2, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 3, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 0, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 1, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 2, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 3, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 0, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 1, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 2, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 3, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 0, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 1, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 2, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 3, 16433153, 1], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 0, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 1, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 2, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 3, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 0, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 1, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 2, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 3, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 0, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 1, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 2, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 3, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 0, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 1, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 2, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 3, 16433153, 4], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 0, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 1, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 2, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 3, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 0, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 1, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 2, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 3, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 0, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 1, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 2, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 3, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 0, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 1, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 2, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 3, 16433153, 8], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 0, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 1, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 2, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 3, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 0, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 1, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 2, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 3, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 0, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 1, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 2, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 3, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 0, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 1, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 2, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 3, 16433153, 16], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 0, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 1, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 2, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 0, 8, 4, 3, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 0, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 1, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 2, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 1, 8, 4, 3, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 0, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 1, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 2, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 8, 8, 4, 3, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 0, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 1, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 2, 16433153, 32], "words": [], "guard": True},
    {"args": ["p32", 64, 8, 4, 3, 16433153, 32], "words": [], "guard": True},
]

INPUTS.extend(
    [
        {
            "id": "third_component_sets_count_bound",
            "args": ["p32", 1, 1, 1024, 3, 16433153, 4],
            "words": [],
            "stubs": [
                {
                    "address": "0x0149ce10",
                    "argc": 2,
                    "ret": 0,
                    "writes": [
                        {
                            "arg": 0,
                            "bytes": "4000000020000000010000000000000007000000000000000000000008000000000000000000000000000000000000000400000000000000",
                        }
                    ],
                },
                {
                    "address": "0x014a27c0",
                    "argc": 3,
                    "ret": 0,
                    "writes": [{"arg": 0, "bytes": "8000000000000000"}, {"arg": 1, "bytes": "40000000"}],
                },
            ],
        }
    ]
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
