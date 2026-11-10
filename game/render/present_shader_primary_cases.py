#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: present_shader_primary_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from copy import deepcopy
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


ADDRESS = "0x02ace1e0"


def cases():
    for optional in (False, True):
        for mask in (0, 1, 0x80000000, 0x8000000000000001):
            c = basic(f"optional_{int(optional)}_mask_{mask:x}", returns="void", size=0xA100)
            c["buffers"].update(
                {
                    "context": {"size": 128, "bytes": "a5" * 128},
                    "descriptor": {"size": 272},
                    "program": {"size": 128},
                    "table": {"size": 2048, "bytes": "7c" * 2048},
                    "guard": {"size": 16},
                }
            )
            c["args"].update(
                {"rsi": "buf:context", "rdx": "buf:descriptor", "rcx": "buf:descriptor" if optional else 0}
            )
            c["memory"] = [
                {"addr": "0x057e3be0", "pointer": "guard"},
                {"addr": "buf:descriptor", "bytes": qword(mask) * 3},
                {"addr": "buf:descriptor+216", "pointer": "table"},
                {"addr": "buf:descriptor+256", "pointer": "program"},
                {"addr": "buf:descriptor+210", "bytes": "01073f"},
                {"addr": "buf:descriptor+240", "bytes": qword(0x0123456789ABCDEF) + qword(0xFEDCBA9876543210)},
            ]
            c["stubs"] = [
                {"address": "0x01475480", "argc": 3, "ret": 0},
                {"address": "0x014753c0", "argc": 2, "ret": 0},
            ]
            yield c

    for flag in (0, 1):
        for selector in (1, 63, 64, 255):
            c = basic(f"link_sparse_flag_{flag}_selector_{selector}", returns="void", size=0xA100)
            c["buffers"]["self"]["bytes"] = "00" * 0xA100
            table = b"".join(struct.pack("<Q", 0x1234000000000000 + i * 0x101) for i in range(256))
            maps = bytes((i * 3 + 5) & 255 for i in range(192))
            c["buffers"].update(
                {
                    "context": {"size": 128, "bytes": "a5" * 128},
                    "descriptor": {"size": 272},
                    "program": {"size": 256},
                    "optional_program": {"size": 256},
                    "optional_descriptor": {"size": 272},
                    "table": {"size": 2048, "bytes": table.hex()},
                }
            )
            c["args"].update({"rsi": "buf:context", "rdx": "buf:descriptor", "rcx": "buf:optional_descriptor"})
            c["memory"] = [memory(4, flag), {"addr": "buf:context+40", "bytes": word(2)}]
            for descriptor, program in (("descriptor", "program"), ("optional_descriptor", "optional_program")):
                c["memory"] += [
                    {"addr": f"buf:{descriptor}", "bytes": qword(0x25) + qword(0x81) + qword(0x8000000080000001)},
                    {"addr": f"buf:{descriptor}+24", "bytes": maps.hex()},
                    {"addr": f"buf:{descriptor}+210", "bytes": bytes((1, selector, selector)).hex()},
                    {"addr": f"buf:{descriptor}+216", "pointer": "table"},
                    {"addr": f"buf:{descriptor}+256", "pointer": program},
                    {"addr": f"buf:{program}+3", "bytes": "01"},
                    {"addr": f"buf:{program}+36", "bytes": "0103"},
                    {"addr": f"buf:{program}+56", "bytes": "03"},
                ]
            c["stubs"] = [
                {"address": "0x01475480", "argc": 3, "ret": 0},
                {"address": "0x014753c0", "argc": 2, "ret": 0},
                {
                    "address": "0x014873a0",
                    "argc": 5,
                    "ret": 0,
                    "writes": [{"arg": 0, "bytes": word(0x10203040) + word(0x50607080) + word(0x90ABCDEF), "size": 12}],
                },
                {"address": "0x01475000", "argc": 3, "ret": 0, "argmem": [{"arg": 1, "size": 12}]},
            ]
            yield c

    # Keep the mask flag set while clearing its independent index gate in both descriptors.
    special = deepcopy(c)
    special["id"] = "special_mask_flag_with_zero_index"
    for descriptor in ("descriptor", "optional_descriptor"):
        special["memory"].append({"addr": f"buf:{descriptor}+210", "bytes": "00"})
    yield special


    maximum = deepcopy(c)
    maximum["id"] = "maximum_link_word_count"
    maximum["memory"].append({"addr": "buf:optional_program+56", "bytes": "20"})
    for stub in maximum["stubs"]:
        if stub["address"] == "0x014873a0":
            stub["writes"] = [{"arg": 0, "bytes": b"".join(
                struct.pack("<I", 0x10203040 + i * 0x01010101) for i in range(32)
            ).hex(), "size": 128}]
        elif stub["address"] == "0x01475000":
            stub["argmem"] = [{"arg": 1, "size": 128}]
    yield maximum


def main():
    """Write deterministic synthetic cases to the requested directory."""
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
