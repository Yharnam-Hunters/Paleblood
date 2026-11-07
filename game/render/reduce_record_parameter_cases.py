#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic scalar selector boundaries and noncanonical low-byte flags.

Usage: reduce_record_parameter_cases.py OUT_DIR. These are authored edge cases, not
recordings from a game run. Table entries remain in the original image.
"""
import json
from pathlib import Path
import sys

ADDRESS = "0x02ab7e40"
INPUTS = [
    (0,),
    (1,),
    (2,),
    (3,),
    (4,),
    (7,),
    (8,),
    (15,),
    (16,),
    (31,),
    (32,),
    (63,),
    (64,),
    (127,),
    (128,),
    (255,),
    (256,),
    (2147483647,),
    (2147483648,),
    (4294967295,),
]


def cases():
    registers = ("rdi", "rsi", "rdx")
    for index, arguments in enumerate(INPUTS):
        yield {
            "schema": 1,
            "address": ADDRESS,
            "id": f"edge_{index:04d}",
            "returns": "i32",
            "args": {registers[i]: hex(value) for i, value in enumerate(arguments)},
            "buffers": {},
        }


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for case in generated:
        (destination / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
