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

ADDRESS = "0x01489960"
RETURN = "i32"
INPUTS = [
    {"args": ["p33"], "words": [(37, 0), (45, 2234652999)]},
    {"args": ["p33"], "words": [(37, 4294967295), (45, 2060314296)]},
    {"args": ["p33"], "words": [(37, 3758096383), (45, 1523443384)]},
    {"args": ["p33"], "words": [(37, 3758096384), (45, 1697782087)]},
    {"args": ["p33"], "words": [(37, 4026531839), (45, 1791878840)]},
    {"args": ["p33"], "words": [(37, 2148466688), (45, 87890247)]},
    {"args": ["p33"], "words": [(37, 3758424064), (45, 1698109767)]},
    {"args": ["p33"], "words": [(37, 3758030848), (45, 1523390791)]},
    {"args": ["p33"], "words": [(37, 0), (45, 2234652999)]},
    {"args": ["p33"], "words": [(37, 65536), (45, 2234718535)]},
    {"args": ["p33"], "words": [(37, 131072), (45, 2234521927)]},
    {"args": ["p33"], "words": [(37, 196608), (45, 2234587463)]},
    {"args": ["p33"], "words": [(37, 262144), (45, 2234915143)]},
    {"args": ["p33"], "words": [(37, 327680), (45, 2234980679)]},
    {"args": ["p33"], "words": [(37, 393216), (45, 2234784071)]},
    {"args": ["p33"], "words": [(37, 458752), (45, 2234849607)]},
    {"args": ["p33"], "words": [(37, 524288), (45, 2235177287)]},
    {"args": ["p33"], "words": [(37, 589824), (45, 2235242823)]},
    {"args": ["p33"], "words": [(37, 655360), (45, 2235046215)]},
    {"args": ["p33"], "words": [(37, 720896), (45, 2235111751)]},
    {"args": ["p33"], "words": [(37, 786432), (45, 2235439431)]},
    {"args": ["p33"], "words": [(37, 851968), (45, 2235504967)]},
    {"args": ["p33"], "words": [(37, 917504), (45, 2235308359)]},
    {"args": ["p33"], "words": [(37, 983040), (45, 2235373895)]},
]


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
