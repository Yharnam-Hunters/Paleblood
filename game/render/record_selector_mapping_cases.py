#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic normalization status and complete selector-table cases.

No game recordings or concurrency equivalence are claimed.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x02ab7310"
RETURN = "i32"
INPUTS = [
    {"args": ["p33", 0], "words": []},
    {"args": ["p33", 1], "words": []},
    {"args": ["p33", 2], "words": []},
    {"args": ["p33", 3], "words": []},
    {"args": ["p33", 4], "words": []},
    {"args": ["p33", 5], "words": []},
    {"args": ["p33", 6], "words": []},
    {"args": ["p33", 7], "words": []},
    {"args": ["p33", 8], "words": []},
    {"args": ["p33", 9], "words": []},
    {"args": ["p33", 10], "words": []},
    {"args": ["p33", 11], "words": []},
    {"args": ["p33", 12], "words": []},
    {"args": ["p33", 13], "words": []},
    {"args": ["p33", 14], "words": []},
    {"args": ["p33", 15], "words": []},
    {"args": ["p33", 16], "words": []},
    {"args": ["p33", 17], "words": []},
    {"args": ["p33", 18], "words": []},
    {"args": ["p33", 19], "words": []},
    {"args": ["p33", 20], "words": []},
    {"args": ["p33", 21], "words": []},
    {"args": ["p33", 22], "words": []},
    {"args": ["p33", 23], "words": []},
    {"args": ["p33", 24], "words": []},
    {"args": ["p33", 25], "words": []},
    {"args": ["p33", 26], "words": []},
    {"args": ["p33", 27], "words": []},
    {"args": ["p33", 28], "words": []},
    {"args": ["p33", 29], "words": []},
    {"args": ["p33", 30], "words": []},
    {"args": ["p33", 31], "words": []},
    {"args": ["p33", 32], "words": []},
    {"args": ["p33", 33], "words": []},
    {"args": ["p33", 34], "words": []},
    {"args": ["p33", 35], "words": []},
    {"args": ["p33", 36], "words": []},
    {"args": ["p33", 37], "words": []},
    {"args": ["p33", 38], "words": []},
    {"args": ["p33", 39], "words": []},
    {"args": ["p33", 40], "words": []},
    {"args": ["p33", 41], "words": []},
    {"args": ["p33", 42], "words": []},
    {"args": ["p33", 43], "words": []},
    {"args": ["p33", 44], "words": []},
    {"args": ["p33", 45], "words": []},
    {"args": ["p33", 46], "words": []},
    {"args": ["p33", 47], "words": []},
    {"args": ["p33", 48], "words": []},
    {"args": ["p33", 49], "words": []},
    {"args": ["p33", 50], "words": []},
    {"args": ["p33", 51], "words": []},
    {"args": ["p33", 52], "words": []},
    {"args": ["p33", 53], "words": []},
    {"args": ["p33", 54], "words": []},
    {"args": ["p33", 55], "words": []},
    {"args": ["p33", 56], "words": []},
    {"args": ["p33", 57], "words": []},
    {"args": ["p33", 58], "words": []},
    {"args": ["p33", 59], "words": []},
    {"args": ["p33", 60], "words": []},
    {"args": ["p33", 61], "words": []},
    {"args": ["p33", 62], "words": []},
    {"args": ["p33", 63], "words": []},
    {"args": ["p33", 64], "words": []},
    {"args": ["p33", 127], "words": []},
    {"args": ["p33", 255], "words": []},
    {"args": ["p33", 4294967295], "words": []},
]

INPUTS.extend(
    [
        {"id": "selector_41", "args": ["p33", 65], "words": []},
        {"id": "selector_42", "args": ["p33", 66], "words": []},
        {"id": "selector_43", "args": ["p33", 67], "words": []},
        {"id": "selector_44", "args": ["p33", 68], "words": []},
        {"id": "selector_45", "args": ["p33", 69], "words": []},
        {"id": "selector_46", "args": ["p33", 70], "words": []},
        {"id": "selector_47", "args": ["p33", 71], "words": []},
        {"id": "selector_48", "args": ["p33", 72], "words": []},
        {"id": "selector_49", "args": ["p33", 73], "words": []},
        {"id": "selector_4a", "args": ["p33", 74], "words": []},
        {"id": "selector_4b", "args": ["p33", 75], "words": []},
        {"id": "selector_4c", "args": ["p33", 76], "words": []},
        {"id": "selector_4d", "args": ["p33", 77], "words": []},
        {"id": "selector_4e", "args": ["p33", 78], "words": []},
        {"id": "selector_4f", "args": ["p33", 79], "words": []},
        {"id": "selector_50", "args": ["p33", 80], "words": []},
        {"id": "selector_51", "args": ["p33", 81], "words": []},
        {"id": "selector_52", "args": ["p33", 82], "words": []},
        {"id": "selector_53", "args": ["p33", 83], "words": []},
        {"id": "selector_54", "args": ["p33", 84], "words": []},
        {"id": "selector_55", "args": ["p33", 85], "words": []},
        {"id": "selector_56", "args": ["p33", 86], "words": []},
        {"id": "selector_57", "args": ["p33", 87], "words": []},
        {"id": "selector_58", "args": ["p33", 88], "words": []},
        {"id": "selector_59", "args": ["p33", 89], "words": []},
        {"id": "selector_5a", "args": ["p33", 90], "words": []},
        {"id": "selector_5b", "args": ["p33", 91], "words": []},
        {"id": "selector_5c", "args": ["p33", 92], "words": []},
        {"id": "selector_5d", "args": ["p33", 93], "words": []},
        {"id": "selector_5e", "args": ["p33", 94], "words": []},
        {"id": "selector_5f", "args": ["p33", 95], "words": []},
        {"id": "selector_60", "args": ["p33", 96], "words": []},
        {"id": "selector_61", "args": ["p33", 97], "words": []},
        {"id": "selector_62", "args": ["p33", 98], "words": []},
        {"id": "selector_63", "args": ["p33", 99], "words": []},
        {"id": "selector_64", "args": ["p33", 100], "words": []},
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
