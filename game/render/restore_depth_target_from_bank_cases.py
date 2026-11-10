#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic depth-target restore cases; helper effects are not game recordings.

Usage: restore_depth_target_from_bank_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x02ad1e70"
MASK64 = (1 << 64) - 1


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def memory(offset, value):
    return {"addr": f"buf:self+{offset}", "bytes": word(value)}


def basic(case_id):
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": case_id,
        "returns": "void",
        "args": {"rdi": "buf:self"},
        "buffers": {"self": {"size": 512, "bytes": "00" * 512}},
        "memory": [],
        "stubs": [],
    }


def cases():
    """Vary the selected bank and absent or present target candidates."""
    for flags in (0, 1, 2, 3):
        for choice in (0, MASK64, "buf:record"):
            case_id = f"flags_{flags}_choice_{str(choice).replace(':', '_')}"
            entry = basic(case_id)
            entry["buffers"].update({"context": {"size": 64}, "record": {"size": 128}})
            entry["args"].update({"rsi": "buf:context", "rdx": 2})
            bank = (flags >> 1) & 1
            offset = 0x60 + bank * 0x48 + 0x40
            entry["memory"] = [memory(0x188, flags)]
            candidate = {"addr": f"buf:self+{offset}"}
            if isinstance(choice, str):
                candidate["pointer"] = "record"
            else:
                candidate["bytes"] = qword(choice)
            entry["memory"].append(candidate)
            entry["stubs"] = [{"address": "0x014739b0", "argc": 2, "ret": 0}]
            yield entry


def main():
    """Write the deterministic synthetic fixture suite to the requested directory."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        (output / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
