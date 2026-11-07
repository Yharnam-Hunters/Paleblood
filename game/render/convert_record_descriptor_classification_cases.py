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

ADDRESS = "0x0149cc60"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", 0, 0], "words": []},
    {"args": ["p33", 0, 1], "words": []},
    {"args": ["p33", 0, 2], "words": []},
    {"args": ["p33", 0, 4], "words": []},
    {"args": ["p33", 0, 8], "words": []},
    {"args": ["p33", 0, 13], "words": []},
    {"args": ["p33", 0, 16], "words": []},
    {"args": ["p33", 1, 0], "words": []},
    {"args": ["p33", 1, 1], "words": []},
    {"args": ["p33", 1, 2], "words": []},
    {"args": ["p33", 1, 4], "words": []},
    {"args": ["p33", 1, 8], "words": []},
    {"args": ["p33", 1, 13], "words": []},
    {"args": ["p33", 1, 16], "words": []},
    {"args": ["p33", 2, 0], "words": []},
    {"args": ["p33", 2, 1], "words": []},
    {"args": ["p33", 2, 2], "words": []},
    {"args": ["p33", 2, 4], "words": []},
    {"args": ["p33", 2, 8], "words": []},
    {"args": ["p33", 2, 13], "words": []},
    {"args": ["p33", 2, 16], "words": []},
    {"args": ["p33", 3, 0], "words": []},
    {"args": ["p33", 3, 1], "words": []},
    {"args": ["p33", 3, 2], "words": []},
    {"args": ["p33", 3, 4], "words": []},
    {"args": ["p33", 3, 8], "words": []},
    {"args": ["p33", 3, 13], "words": []},
    {"args": ["p33", 3, 16], "words": []},
    {"args": ["p33", 4, 0], "words": []},
    {"args": ["p33", 4, 1], "words": []},
    {"args": ["p33", 4, 2], "words": []},
    {"args": ["p33", 4, 4], "words": []},
    {"args": ["p33", 4, 8], "words": []},
    {"args": ["p33", 4, 13], "words": []},
    {"args": ["p33", 4, 16], "words": []},
    {"args": ["p33", 5, 0], "words": []},
    {"args": ["p33", 5, 1], "words": []},
    {"args": ["p33", 5, 2], "words": []},
    {"args": ["p33", 5, 4], "words": []},
    {"args": ["p33", 5, 8], "words": []},
    {"args": ["p33", 5, 13], "words": []},
    {"args": ["p33", 5, 16], "words": []},
    {"args": ["p33", 6, 0], "words": []},
    {"args": ["p33", 6, 1], "words": []},
    {"args": ["p33", 6, 2], "words": []},
    {"args": ["p33", 6, 4], "words": []},
    {"args": ["p33", 6, 8], "words": []},
    {"args": ["p33", 6, 13], "words": []},
    {"args": ["p33", 6, 16], "words": []},
    {"args": ["p33", 7, 0], "words": []},
    {"args": ["p33", 7, 1], "words": []},
    {"args": ["p33", 7, 2], "words": []},
    {"args": ["p33", 7, 4], "words": []},
    {"args": ["p33", 7, 8], "words": []},
    {"args": ["p33", 7, 13], "words": []},
    {"args": ["p33", 7, 16], "words": []},
    {"args": ["p33", 8, 0], "words": []},
    {"args": ["p33", 8, 1], "words": []},
    {"args": ["p33", 8, 2], "words": []},
    {"args": ["p33", 8, 4], "words": []},
    {"args": ["p33", 8, 8], "words": []},
    {"args": ["p33", 8, 13], "words": []},
    {"args": ["p33", 8, 16], "words": []},
    {"args": ["p33", 9, 0], "words": []},
    {"args": ["p33", 9, 1], "words": []},
    {"args": ["p33", 9, 2], "words": []},
    {"args": ["p33", 9, 4], "words": []},
    {"args": ["p33", 9, 8], "words": []},
    {"args": ["p33", 9, 13], "words": []},
    {"args": ["p33", 9, 16], "words": []},
    {"args": ["p33", 10, 0], "words": []},
    {"args": ["p33", 10, 1], "words": []},
    {"args": ["p33", 10, 2], "words": []},
    {"args": ["p33", 10, 4], "words": []},
    {"args": ["p33", 10, 8], "words": []},
    {"args": ["p33", 10, 13], "words": []},
    {"args": ["p33", 10, 16], "words": []},
    {"args": ["p33", 11, 0], "words": []},
    {"args": ["p33", 11, 1], "words": []},
    {"args": ["p33", 11, 2], "words": []},
    {"args": ["p33", 11, 4], "words": []},
    {"args": ["p33", 11, 8], "words": []},
    {"args": ["p33", 11, 13], "words": []},
    {"args": ["p33", 11, 16], "words": []},
    {"args": ["p33", 12, 0], "words": []},
    {"args": ["p33", 12, 1], "words": []},
    {"args": ["p33", 12, 2], "words": []},
    {"args": ["p33", 12, 4], "words": []},
    {"args": ["p33", 12, 8], "words": []},
    {"args": ["p33", 12, 13], "words": []},
    {"args": ["p33", 12, 16], "words": []},
    {"args": ["p33", 13, 0], "words": []},
    {"args": ["p33", 13, 1], "words": []},
    {"args": ["p33", 13, 2], "words": []},
    {"args": ["p33", 13, 4], "words": []},
    {"args": ["p33", 13, 8], "words": []},
    {"args": ["p33", 13, 13], "words": []},
    {"args": ["p33", 13, 16], "words": []},
    {"args": ["p33", 14, 0], "words": []},
    {"args": ["p33", 14, 1], "words": []},
    {"args": ["p33", 14, 2], "words": []},
    {"args": ["p33", 14, 4], "words": []},
    {"args": ["p33", 14, 8], "words": []},
    {"args": ["p33", 14, 13], "words": []},
    {"args": ["p33", 14, 16], "words": []},
    {"args": ["p33", 15, 0], "words": []},
    {"args": ["p33", 15, 1], "words": []},
    {"args": ["p33", 15, 2], "words": []},
    {"args": ["p33", 15, 4], "words": []},
    {"args": ["p33", 15, 8], "words": []},
    {"args": ["p33", 15, 13], "words": []},
    {"args": ["p33", 15, 16], "words": []},
    {"args": ["p33", 16, 0], "words": []},
    {"args": ["p33", 16, 1], "words": []},
    {"args": ["p33", 16, 2], "words": []},
    {"args": ["p33", 16, 4], "words": []},
    {"args": ["p33", 16, 8], "words": []},
    {"args": ["p33", 16, 13], "words": []},
    {"args": ["p33", 16, 16], "words": []},
    {"args": ["p33", 17, 0], "words": []},
    {"args": ["p33", 17, 1], "words": []},
    {"args": ["p33", 17, 2], "words": []},
    {"args": ["p33", 17, 4], "words": []},
    {"args": ["p33", 17, 8], "words": []},
    {"args": ["p33", 17, 13], "words": []},
    {"args": ["p33", 17, 16], "words": []},
    {"args": ["p33", 18, 0], "words": []},
    {"args": ["p33", 18, 1], "words": []},
    {"args": ["p33", 18, 2], "words": []},
    {"args": ["p33", 18, 4], "words": []},
    {"args": ["p33", 18, 8], "words": []},
    {"args": ["p33", 18, 13], "words": []},
    {"args": ["p33", 18, 16], "words": []},
    {"args": ["p33", 19, 0], "words": []},
    {"args": ["p33", 19, 1], "words": []},
    {"args": ["p33", 19, 2], "words": []},
    {"args": ["p33", 19, 4], "words": []},
    {"args": ["p33", 19, 8], "words": []},
    {"args": ["p33", 19, 13], "words": []},
    {"args": ["p33", 19, 16], "words": []},
    {"args": ["p33", 20, 0], "words": []},
    {"args": ["p33", 20, 1], "words": []},
    {"args": ["p33", 20, 2], "words": []},
    {"args": ["p33", 20, 4], "words": []},
    {"args": ["p33", 20, 8], "words": []},
    {"args": ["p33", 20, 13], "words": []},
    {"args": ["p33", 20, 16], "words": []},
    {"args": ["p33", 21, 0], "words": []},
    {"args": ["p33", 21, 1], "words": []},
    {"args": ["p33", 21, 2], "words": []},
    {"args": ["p33", 21, 4], "words": []},
    {"args": ["p33", 21, 8], "words": []},
    {"args": ["p33", 21, 13], "words": []},
    {"args": ["p33", 21, 16], "words": []},
    {"args": ["p33", 22, 0], "words": []},
    {"args": ["p33", 22, 1], "words": []},
    {"args": ["p33", 22, 2], "words": []},
    {"args": ["p33", 22, 4], "words": []},
    {"args": ["p33", 22, 8], "words": []},
    {"args": ["p33", 22, 13], "words": []},
    {"args": ["p33", 22, 16], "words": []},
    {"args": ["p33", 23, 0], "words": []},
    {"args": ["p33", 23, 1], "words": []},
    {"args": ["p33", 23, 2], "words": []},
    {"args": ["p33", 23, 4], "words": []},
    {"args": ["p33", 23, 8], "words": []},
    {"args": ["p33", 23, 13], "words": []},
    {"args": ["p33", 23, 16], "words": []},
    {"args": ["p33", 24, 0], "words": []},
    {"args": ["p33", 24, 1], "words": []},
    {"args": ["p33", 24, 2], "words": []},
    {"args": ["p33", 24, 4], "words": []},
    {"args": ["p33", 24, 8], "words": []},
    {"args": ["p33", 24, 13], "words": []},
    {"args": ["p33", 24, 16], "words": []},
    {"args": ["p33", 25, 0], "words": []},
    {"args": ["p33", 25, 1], "words": []},
    {"args": ["p33", 25, 2], "words": []},
    {"args": ["p33", 25, 4], "words": []},
    {"args": ["p33", 25, 8], "words": []},
    {"args": ["p33", 25, 13], "words": []},
    {"args": ["p33", 25, 16], "words": []},
    {"args": ["p33", 26, 0], "words": []},
    {"args": ["p33", 26, 1], "words": []},
    {"args": ["p33", 26, 2], "words": []},
    {"args": ["p33", 26, 4], "words": []},
    {"args": ["p33", 26, 8], "words": []},
    {"args": ["p33", 26, 13], "words": []},
    {"args": ["p33", 26, 16], "words": []},
    {"args": ["p33", 27, 0], "words": []},
    {"args": ["p33", 27, 1], "words": []},
    {"args": ["p33", 27, 2], "words": []},
    {"args": ["p33", 27, 4], "words": []},
    {"args": ["p33", 27, 8], "words": []},
    {"args": ["p33", 27, 13], "words": []},
    {"args": ["p33", 27, 16], "words": []},
    {"args": ["p33", 28, 0], "words": []},
    {"args": ["p33", 28, 1], "words": []},
    {"args": ["p33", 28, 2], "words": []},
    {"args": ["p33", 28, 4], "words": []},
    {"args": ["p33", 28, 8], "words": []},
    {"args": ["p33", 28, 13], "words": []},
    {"args": ["p33", 28, 16], "words": []},
    {"args": ["p33", 29, 0], "words": []},
    {"args": ["p33", 29, 1], "words": []},
    {"args": ["p33", 29, 2], "words": []},
    {"args": ["p33", 29, 4], "words": []},
    {"args": ["p33", 29, 8], "words": []},
    {"args": ["p33", 29, 13], "words": []},
    {"args": ["p33", 29, 16], "words": []},
    {"args": ["p33", 30, 0], "words": []},
    {"args": ["p33", 30, 1], "words": []},
    {"args": ["p33", 30, 2], "words": []},
    {"args": ["p33", 30, 4], "words": []},
    {"args": ["p33", 30, 8], "words": []},
    {"args": ["p33", 30, 13], "words": []},
    {"args": ["p33", 30, 16], "words": []},
    {"args": ["p33", 31, 0], "words": []},
    {"args": ["p33", 31, 1], "words": []},
    {"args": ["p33", 31, 2], "words": []},
    {"args": ["p33", 31, 4], "words": []},
    {"args": ["p33", 31, 8], "words": []},
    {"args": ["p33", 31, 13], "words": []},
    {"args": ["p33", 31, 16], "words": []},
]

# Tile-table index 19 has MICRO_TILE_MODE_NEW=4. Cover every requested ARRAY_MODE.
INPUTS.extend(
    {
        "id": f"thick_category_array_mode_{requested}",
        "args": ["p33", 20 if requested == 3 else 19, requested],
        "words": [],
    }
    for requested in range(2, 14)
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
