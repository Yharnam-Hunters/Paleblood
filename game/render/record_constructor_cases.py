#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic constructor arithmetic, selector and publication boundaries.

No game recordings or race/fault equivalence are claimed.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x01485f90"
RETURN = "i64"
INPUTS = [
    {"args": ["p32", 65, 33, 3, 16433167, 0, 0, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 0, 0, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 0, 1, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 0, 1, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 0, 1, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 0, 4, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 0, 4, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 0, 4, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 1, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 1, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 1, 0, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 1, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 1, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 1, 1, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 1, 4, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 1, 4, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 4, 0, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 4, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 4, 0, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 4, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 4, 1, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 4, 4, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 4, 4, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 4, 4, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 4, 4, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 8, 0, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 8, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 8, 0, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 8, 0, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 8, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 8, 1, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 8, 1, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 8, 4, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 8, 4, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 8, 4, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 16, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 16, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 16, 0, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 16, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 16, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 16, 1, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 16, 4, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 16, 4, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 32, 0, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 32, 0, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 32, 0, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433174, 32, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433153, 32, 1, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 32, 1, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 32, 1, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433165, 32, 4, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433175, 32, 4, 1, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 65, 33, 3, 16433167, 32, 4, 4, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 16433153, 4, 1, 0, 0, 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 16433153, 4, 1, 0, "p96", 0], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 16433153, 4, 1, 0, 0, "p104"], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 16433153, 4, 1, 0, "p96", "p104"], "words": [], "guard": True},
    {"args": ["p32", 64, 64, 1, 16433153, 4, 1, 0, "p33", "p34"], "words": [], "guard": True},
]

INPUTS.extend(
    [
        {
            "id": "signed_selector_high_bit",
            "args": ["p32", 64, 64, 3, 16433153, 4, 2147483648, 0, 0, "p256"],
            "words": [],
            "guard": True,
            "stubs": [
                {
                    "address": "0x0149ce10",
                    "argc": 2,
                    "ret": 0,
                    "writes": [
                        {
                            "arg": 0,
                            "bytes": "4000000040000000010000000000000001000000000000000000000000080000000000000000000000000000000000000400000000000000",
                        }
                    ],
                }
            ],
        },
        {
            "id": "product_exceeds_low_dword",
            "args": ["p32", 64, 64, 4294967295, 16433153, 4, 0, 0, 0, 0],
            "words": [],
            "guard": True,
            "stubs": [
                {
                    "address": "0x0149ce10",
                    "argc": 2,
                    "ret": 0,
                    "writes": [
                        {
                            "arg": 0,
                            "bytes": "4000000040000000010000000000000002000000000000000000000000080000000000000000000000000000000000000400000000000000",
                        }
                    ],
                }
            ],
        },
    ]
)


# Keep the helper's geometry publication controlled while the original category
# and encoding-metadata decoders decide whether the auxiliary output is allowed.
for case_id, encoding, tile_mode in (
    ("first_auxiliary_thick_tile_mode", 0x00FAC001, 19),
    ("first_auxiliary_128_bit_encoding", 0x00FAC00E, 0),
):
    geometry = bytearray(0x38)
    struct.pack_into("<III", geometry, 0, 64, 64, 1)
    struct.pack_into("<Q", geometry, 0x10, 1)
    struct.pack_into("<I", geometry, 0x1C, 0x800)
    struct.pack_into("<I", geometry, 0x30, tile_mode)
    INPUTS.append({
        "id": case_id,
        "args": ["p32", 64, 64, 1, encoding, 4, 1, 0, "p96", 0],
        "words": [],
        "stubs": [
            {"address": "0x0149ce10", "argc": 2, "ret": 0,
             "writes": [{"arg": 0, "bytes": geometry.hex()}]},
            # Removing an admission must expose this otherwise skipped call.
            {"address": "0x0149e2d0", "argc": 4, "ret": 0,
             "writes": [{"arg": 0, "bytes": struct.pack("<Q", 256).hex()},
                        {"arg": 1, "bytes": struct.pack("<I", 0x800).hex()}]},
        ],
    })


# Each optional output aliases the exact record word read after its publication.
for name, first_pointer, second_pointer in (
    ("first_auxiliary_overlaps_later_quantity", "p64", 0),
    ("second_auxiliary_overlaps_later_tail", "p96", "p72"),
):
    stubs = [
        {"address": "0x0149e2d0", "argc": 8, "argi": [0, 1, 2, 3],
         "argu32": [4, 5, 6], "argu8": [7], "ret": 0,
         "writes": [{"arg": 0, "bytes": struct.pack("<Q", 0x55AA0000).hex(), "size": 8},
                    {"arg": 1, "bytes": struct.pack("<I", 1024).hex(), "size": 4}]},
    ]
    if second_pointer:
        stubs.append({"address": "0x0149e400", "argc": 9, "argi": [0, 1, 2, 3],
            "argu32": [4, 5, 6, 7, 8], "ret": 0,
            "writes": [{"arg": 0, "bytes": struct.pack("<Q", 0xABC00000).hex(), "size": 8},
                       {"arg": 1, "bytes": struct.pack("<I", 1024).hex(), "size": 4},
                       {"arg": 2, "bytes": struct.pack("<I", 8).hex(), "size": 4},
                       {"arg": 3, "bytes": struct.pack("<I", 8).hex(), "size": 4}]})
    INPUTS.append({"id": name, "args": ["p32", 64, 64, 1, 16433153, 4, 1, 0,
        first_pointer, second_pointer], "words": [], "guard": True, "stubs": stubs})


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
