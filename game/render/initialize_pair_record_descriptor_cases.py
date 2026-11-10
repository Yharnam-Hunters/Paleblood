#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic pair descriptor flag, signed-level and total-overflow boundaries.

Authored synthetic cases only; no genuine game recordings.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x01487ee0"
RETURN = "i64"
INPUTS = [
    {"args": ["p32", 64, 64, 1, 0, 16433153, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 0, 1, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 0, 0, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 1, 1, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 1, 0, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 1, 1, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 0, 16433153, 4, 1, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 4, 0, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 4, 1, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 4, 0, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 4, 1, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 8, 1, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 8, 0, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 8, 1, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 0, 16433153, 16, 1, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 16, 0, 256], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 16, 1, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 16, 0, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 16, 1, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 0, 16433153, 32, 1, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 32, 0, 2], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 1, 16433153, 32, 1, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 32, 0, 1], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 2, 16433153, 32, 1, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 3, 16433153, 32, 0, 0], "words": [], "guard": True},
]

INPUTS.extend(
    [
        {
            "id": "nonboolean_flag_02",
            "args": ["p32", 64, 32, 1, 1, 16433153, 4, 0, 2],
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
        },
        {
            "id": "nonboolean_flag_03",
            "args": ["p32", 64, 32, 1, 1, 16433153, 4, 0, 3],
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
        },
        {
            "id": "nonboolean_flag_80",
            "args": ["p32", 64, 32, 1, 1, 16433153, 4, 0, 128],
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
        },
        {
            "id": "nonboolean_flag_ff",
            "args": ["p32", 64, 32, 1, 1, 16433153, 4, 0, 255],
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
        },
        {
            "id": "signed_level_high_bit",
            "args": ["p32", 64, 32, 1, 1, 16433153, 4, 2147483648, 0],
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
        },
        {
            "id": "pair_total_overflow",
            "args": ["p32", 64, 32, 1, 1, 16433153, 4, 0, 0],
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
                    "writes": [{"arg": 0, "bytes": "0300000001000000"}, {"arg": 1, "bytes": "40000000"}],
                },
            ],
        },
    ]
)

# A negative level with low bits set must bypass the unsigned level calculation.
from copy import deepcopy

signed_level_case = deepcopy(next(row for row in INPUTS if row.get("id") == "signed_level_high_bit"))
signed_level_case["id"] = "signed_level_high_bit_low_nibble"
signed_level_case["args"][7] = 0x80000005
INPUTS.append(signed_level_case)


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
