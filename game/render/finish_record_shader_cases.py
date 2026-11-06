#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: finish_record_shader_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys

MASK64 = (1 << 64) - 1


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def memory(offset, value):
    return {"addr": f"buf:self+{offset}", "bytes": word(value)}


def basic(cid, returns="i64", size=512):
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": returns,
        "args": {"rdi": "buf:self"},
        "buffers": {"self": {"size": size, "bytes": "a5" * size}},
        "memory": [],
        "stubs": [],
    }


ADDRESS = "0x02acfc20"


def cases():
    for dirty in (0, 0x80, 0x10, 0x90, 0x192, 0xF0, 0xFFFFFFFF):
        for mode in (0xFFFFFFFF, 0, 0x200):
            c = basic(f"dirty_{dirty:x}_mode_{mode:x}", returns="void", size=0xB380)
            c["buffers"]["context"] = {"size": 0xB4A0, "bytes": "a5" * 0xB4A0}
            c["args"]["rsi"] = "buf:context"
            c["memory"] = [memory(0xB368, dirty), memory(0, mode)]
            if mode != 0xFFFFFFFF:
                c["stubs"] = [{"address": "0x01476030", "argc": 2, "ret": 0}]
            yield c

    for fill in ("00", "5a"):
        for pass_bit in (0x80, 0x10, 0x90):
            for flags in (0x02, 0x100, 0x20, 0x40):
                dirty = pass_bit | flags
                c = basic(f"fill_{fill}_pass_{pass_bit:x}_flags_{flags:x}", returns="void", size=0xB380)
                c["buffers"]["context"] = {"size": 0xB4A0, "bytes": fill * 0xB4A0}
                c["args"]["rsi"] = "buf:context"
                c["memory"] = [memory(0xB368, dirty), memory(0, 0xFFFFFFFF)]
                yield c

    # The state's command mode aliases the command context's preparation mode.
    # The first pass sets its active bit before the mode-selection call reads it.
    c = basic("command_mode_aliases_preparation_mode", returns="void", size=0xB380)
    c["buffers"]["context"] = {"size": 0x17000, "bytes": "a5" * 0x17000}
    c["args"].update({"rdi": "buf:context+0xb488", "rsi": "buf:context"})
    c["memory"] = [
        {"addr": f"buf:context+{0xB488 + 0xB368}", "bytes": word(0x80)},
        {"addr": "buf:context+0xb488", "bytes": word(0x200)},
    ]
    c["stubs"] = [{"address": "0x01476030", "argc": 2, "ret": 0}]
    yield c


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
