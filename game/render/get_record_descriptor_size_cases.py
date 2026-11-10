#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic descriptor size loop and power-of-two factor rounding.

Authored synthetic cases only; no genuine game recordings.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x014a27c0"
RETURN = "i32"
INPUTS = [
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2416054188),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2684489644),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2952925100),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 3489796012),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2420248492),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2688683948),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2957119404),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p32", "p48", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 3493990316),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p136", "p140", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2416054188),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p33", "p34", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2416054188),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
    {
        "args": ["p128", "p128", "p128"],
        "words": [
            (128, 0),
            (132, 1048576),
            (136, 507967),
            (140, 2416054188),
            (144, 516099),
            (148, 0),
            (152, 0),
            (156, 0),
        ],
        "guard": True,
    },
]

INPUTS.extend(
    [
        {
            "id": "rounded_descriptor_factor_three_to_four",
            "args": ["p32", "p40", "p128"],
            "words": [(128, 0), (132, 1048576), (136, 0), (140, 2420113408), (144, 2), (148, 0), (152, 0), (156, 0)],
            "stubs": [
                {
                    "address": "0x014a1e80",
                    "argc": 4,
                    "ret": 0,
                    "writes": [
                        {
                            "arg": 0,
                            "bytes": "040000000000000001000000010000000100000001000000000000000000000000000000001000004000000000000000",
                        }
                    ],
                },
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
