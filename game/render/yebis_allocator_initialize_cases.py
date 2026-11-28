#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic allocator-interface initialization cases. Usage: SCRIPT OUT_DIR.

Owned aligned storage exposes the complete pointer store and untouched neighboring
bytes. Original vtables are borrowed only as values, with no writes to the image.
The observed caller discards the physical RAX pointer; the logical result is void.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6f10"
ALLOCATOR_VTABLE = "0x056b3630"
INTERFACE_VTABLE = "0x056b35e0"
PREFIX_BYTES = 8
OWNED_BYTES = 0x80


def case(cid, offset=16, fill="a5", size=OWNED_BYTES):
    """Supply an aligned interface prefix and independent spare storage."""
    assert offset >= 0 and offset % 8 == 0 and offset + PREFIX_BYTES <= size
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
            "args": {"rdi": f"buf:object+{offset}", "rsi": "buf:spare+16",
                     "rax": "0xfedcba9876543210", "rdx": "0x100000000",
                     "rcx": "0xffffffffffffffff", "r8": "0x8000000000000000",
                     "r9": "0x1"},
            "buffers": {"object": {"size": size, "bytes": fill * size},
                        "spare": {"size": OWNED_BYTES, "bytes": "d7" * OWNED_BYTES}},
            "memory": [], "stubs": []}


def put(row, relative, data):
    """Set owned bytes relative to the selected interface prefix."""
    offset = int(row["args"]["rdi"].rsplit("+", 1)[1])
    row["memory"].append({"addr": f"buf:object+{offset + relative}", "bytes": data.hex()})


def cases():
    """Distinguish store widths, the exact footprint and ignored inputs."""
    result = []
    for offset in [0, 8, 16, 24, 64]:
        for fill in ["00", "55", "a5", "ff"]:
            result.append(case(f"prefix-{offset}-{fill}", offset, fill))
    for fill in ["00", "a5", "ff"]:
        result.append(case(f"exact-prefix-{fill}", 0, fill, PREFIX_BYTES))
    for name, address in [("allocator", ALLOCATOR_VTABLE), ("interface", INTERFACE_VTABLE)]:
        row = case("initial-vtable-" + name)
        row["memory"].append({"addr": row["args"]["rdi"], "guest": address})
        result.append(row)
    row = case("initial-vtable-null")
    put(row, 0, bytes(PREFIX_BYTES))
    result.append(row)
    for offset in [8, 12, 16, 24]:
        row = case(f"neighbor-{offset}", fill="00")
        put(row, offset, struct.pack("<I", 0x80000001))
        result.append(row)
    for name, value in [("all-bits", "0xffffffffffffffff"), ("upper-word", "0x100000000")]:
        row = case("ignored-general-" + name)
        for register in ["rax", "rsi", "rdx", "rcx", "r8", "r9"]:
            row["args"][register] = value
        result.append(row)
    for name, value in [("finite", "0000803f000000400000404000008040"),
                        ("nan-payloads", "0100807f" * 4)]:
        row = case("ignored-xmm-" + name)
        row["args"].update(xmm0=value, xmm1=value)
        result.append(row)
    for name, values in [("zero", ["0x0"] * 4),
                         ("poison", ["0xffffffffffffffff", "0x100000000",
                                     "buf:spare+16", "0x8000000000000000"])]:
        row = case("ignored-stack-" + name)
        row["stack"] = values
        result.append(row)
    result.append(case("derived-tail-preserved", fill="9d", size=0x200))
    row = case("distinct-object-bytes")
    row["buffers"]["object"]["bytes"] = bytes(range(OWNED_BYTES)).hex()
    result.append(row)
    for fill in ["00", "ff"]:
        result.append(case("last-prefix-" + fill, OWNED_BYTES - PREFIX_BYTES, fill))
    return result


def main():
    """Generate deterministic distinct fixtures outside Git."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == len({row["id"] for row in generated}) == 40
    inputs = [json.dumps({key: value for key, value in row.items() if key != "id"},
                         sort_keys=True) for row in generated]
    assert len(set(inputs)) == len(generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
