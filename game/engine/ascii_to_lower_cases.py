#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic ASCII byte inputs; no expected return-value oracle."""

import json
from pathlib import Path
import sys


def case(case_id, value, incoming=None, pattern=None):
    """Vary the selected byte, ignored GP inputs and untouched canary bytes."""
    if not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("incoming GP value outside 64 bits")
    args = {reg: "0x0" for reg in ("rdi", "rsi", "rdx", "rcx", "r8", "r9")}
    args["rdi"] = hex(value)
    if "rdi" in (incoming or {}):
        raise ValueError("ignored seeds cannot replace the formal input")
    args.update(incoming or {})
    canary = bytes(
        ((0x53 + 19 * index) & 255) if pattern is None else pattern for index in range(64)
    )
    return {
        "schema": 1,
        "address": "0x011c48f0",
        "id": case_id,
        "returns": "i64",
        "args": args,
        "buffers": {"probe": {"size": 64, "bytes": canary.hex()}},
        "memory": [],
        "imports": [],
        "stubs": [],
    }


CASES = [case(f"byte_{value:02x}", value) for value in range(256)]
UPPER_SEEDS = [0x100, 0x1234567800000000, 0xFFFFFFFFFFFFFF00, 0x8000000000000000]
LOW_BYTES = [0x00, 0x40, 0x41, 0x5A, 0x5B, 0x7F, 0x80, 0xFF]
for index, upper in enumerate(UPPER_SEEDS):
    CASES.extend(case(f"upper_seed{index}_byte_{value:02x}", upper | value) for value in LOW_BYTES)
CASES.extend(
    [
        case(
            "ignored_gp_all_bits",
            0x41,
            {
                "rsi": "0xffffffffffffffff",
                "rdx": "0x1234567887654321",
                "rcx": "0x8000000000000000",
                "r8": "0xfeedface",
                "r9": "0x7fffffffffffffff",
            },
        ),
        case(
            "ignored_gp_mapped_canaries",
            0x80,
            {
                "rsi": "buf:probe+0x1",
                "rdx": "buf:probe+0x9",
                "rcx": "buf:probe+0x11",
                "r8": "buf:probe+0x19",
                "r9": "buf:probe+0x21",
            },
        ),
        case(
            "ignored_gp_zero_canary",
            0xFF,
            {
                "rsi": "0x1",
                "rdx": "0xffffffffffffffff",
                "rcx": "0x2",
                "r8": "0x8000000000000000",
                "r9": "0x12345678ffffffff",
            },
            pattern=0,
        ),
    ]
)


def main():
    if len(sys.argv) != 2:
        print(f"usage: {Path(__file__).name} OUT_DIR", file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        path = destination / (fixture["id"] + ".json")
        path.write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(CASES)} authored input states written to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
