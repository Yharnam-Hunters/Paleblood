#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic scalar selector boundaries and noncanonical low-byte flags.

Usage: choose_record_constructor_selector_cases.py OUT_DIR. These are authored edge cases, not
recordings from a game run. Table entries remain in the original image.
"""
import json
from pathlib import Path
import sys

ADDRESS = "0x02ab7a20"
INPUTS = [
    (0, 0),
    (0, 1),
    (0, 2),
    (0, 255),
    (0, 256),
    (0, 257),
    (0, 4294967295),
    (1, 0),
    (1, 1),
    (1, 2),
    (1, 255),
    (1, 256),
    (1, 257),
    (1, 4294967295),
    (2, 0),
    (2, 1),
    (2, 2),
    (2, 255),
    (2, 256),
    (2, 257),
    (2, 4294967295),
    (3, 0),
    (3, 1),
    (3, 2),
    (3, 255),
    (3, 256),
    (3, 257),
    (3, 4294967295),
    (4, 0),
    (4, 1),
    (4, 2),
    (4, 255),
    (4, 256),
    (4, 257),
    (4, 4294967295),
    (7, 0),
    (7, 1),
    (7, 2),
    (7, 255),
    (7, 256),
    (7, 257),
    (7, 4294967295),
    (8, 0),
    (8, 1),
    (8, 2),
    (8, 255),
    (8, 256),
    (8, 257),
    (8, 4294967295),
    (15, 0),
    (15, 1),
    (15, 2),
    (15, 255),
    (15, 256),
    (15, 257),
    (15, 4294967295),
    (16, 0),
    (16, 1),
    (16, 2),
    (16, 255),
    (16, 256),
    (16, 257),
    (16, 4294967295),
    (31, 0),
    (31, 1),
    (31, 2),
    (31, 255),
    (31, 256),
    (31, 257),
    (31, 4294967295),
    (32, 0),
    (32, 1),
    (32, 2),
    (32, 255),
    (32, 256),
    (32, 257),
    (32, 4294967295),
    (63, 0),
    (63, 1),
    (63, 2),
    (63, 255),
    (63, 256),
    (63, 257),
    (63, 4294967295),
    (64, 0),
    (64, 1),
    (64, 2),
    (64, 255),
    (64, 256),
    (64, 257),
    (64, 4294967295),
    (127, 0),
    (127, 1),
    (127, 2),
    (127, 255),
    (127, 256),
    (127, 257),
    (127, 4294967295),
    (128, 0),
    (128, 1),
    (128, 2),
    (128, 255),
    (128, 256),
    (128, 257),
    (128, 4294967295),
    (255, 0),
    (255, 1),
    (255, 2),
    (255, 255),
    (255, 256),
    (255, 257),
    (255, 4294967295),
    (256, 0),
    (256, 1),
    (256, 2),
    (256, 255),
    (256, 256),
    (256, 257),
    (256, 4294967295),
    (2147483647, 0),
    (2147483647, 1),
    (2147483647, 2),
    (2147483647, 255),
    (2147483647, 256),
    (2147483647, 257),
    (2147483647, 4294967295),
    (2147483648, 0),
    (2147483648, 1),
    (2147483648, 2),
    (2147483648, 255),
    (2147483648, 256),
    (2147483648, 257),
    (2147483648, 4294967295),
    (4294967295, 0),
    (4294967295, 1),
    (4294967295, 2),
    (4294967295, 255),
    (4294967295, 256),
    (4294967295, 257),
    (4294967295, 4294967295),
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
