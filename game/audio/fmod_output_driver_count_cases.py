#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""FMOD output callback: write one driver when the output is present."""

import json
from functools import partial
from fmod_output_cases import make_case
from pathlib import Path
import sys

ADDRESS = "0x0111b060"
OUTPUTS = ["rsi"]

case = partial(make_case, address=ADDRESS, outputs=OUTPUTS)
CASES = [
    case("null_default", [None]),
    case("aligned_offset0", [0]),
    case("aligned_offset8", [8]),
    case("unaligned_offset1", [1]),
    case("unaligned_offset2", [2]),
    case("unaligned_offset3", [3]),
    case("unaligned_offset5", [5]),
    case("unaligned_offset7", [7]),
    case("last_four_bytes", [60]),
    case("exact_four_byte_buffer", [0], arena_size=4),
    case("unaligned_last_four", [61], arena_size=65),
    case("zero_pattern", [8], pattern=0),
    case("all_bits_pattern", [8], pattern=255),
    case("existing_word_one", [8], initial_words=[(8, 1)]),
    case("ignored_slot_mapped", [8], incoming={"rdi": "buf:ignored+0x8"}),
    case("unaligned_ignored_slot_mapped", [9], incoming={"rdi": "buf:ignored+0x9"}),
    case(
        "ignored_gp_all_bits",
        [8],
        incoming={
            "rdi": "0xffffffffffffffff",
            "rdx": "0x1234567887654321",
            "rcx": "0x8000000000000000",
            "r8": "0xfeedface",
            "r9": "0x7fffffffffffffff",
        },
    ),
    case(
        "null_ignored_gp_opaque",
        [None],
        incoming={
            "rdi": "0xfedcba9876543210",
            "rdx": "0xffffffffffffffff",
            "rcx": "0x1",
            "r8": "0x8000000000000000",
            "r9": "0x12345678ffffffff",
        },
    ),
]


def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        print(f"usage: {Path(__file__).name} OUT_DIR", file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (destination / (fixture["id"] + ".json")).write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(CASES)} authored synthetic inputs written to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
