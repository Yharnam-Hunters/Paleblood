#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic IPfxInterface initialization cases. Usage: SCRIPT OUT_DIR.

All storage is owned. The cases observe the initialized prefix, its untouched
padding and neighboring bytes. No callees, imports or original-image writes
are scripted. A void result excludes the original's incidental RAX carrier.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6d20"
INTERFACE_VTABLE = "0x056b35e0"
CONTEXT_VTABLE = "0x056b3580"
PREFIX_BYTES = 0x18
OWNED_BYTES = 0x100
TAIL_TEST_BYTES = 0x200


def case(cid, offset=16, fill="a5", size=OWNED_BYTES):
    """Provide an aligned writable prefix plus an independent spare object."""
    assert offset >= 0 and offset % 8 == 0 and offset + PREFIX_BYTES <= size
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
            "args": {"rdi": f"buf:object+{offset}", "rsi": "buf:spare+16",
                     "rax": "0xfedcba9876543210", "rdx": "0xffffffffffffffff",
                     "rcx": "0x100000000", "r8": "0x8000000000000000", "r9": "0x1"},
            "buffers": {"object": {"size": size, "bytes": fill * size},
                        "spare": {"size": OWNED_BYTES, "bytes": "d7" * OWNED_BYTES}},
            "memory": [], "stubs": []}


def put(row, offset, data):
    """Set up only the owned object, relative to its selected prefix."""
    start = int(row["args"]["rdi"].rsplit("+", 1)[1])
    row["memory"].append({"addr": f"buf:object+{start + offset}", "bytes": data.hex()})


def put_vtable(row, address):
    """Borrow an image address as a value without writing the original table."""
    row["memory"].append({"addr": row["args"]["rdi"], "guest": address})


def cases():
    """Distinguish field widths, preservation, valid bounds and ignored inputs."""
    result = []
    for offset in [0, 8, 16, 24, 64]:
        for fill in ["00", "55", "a5", "ff"]:
            result.append(case(f"prefix-{offset}-{fill}", offset, fill))
    for fill in ["00", "a5", "ff"]:
        result.append(case(f"exact-prefix-{fill}", 0, fill, PREFIX_BYTES))
    for index in range(4):
        row = case(f"padding-byte-{index}", fill="00")
        put_vtable(row, INTERFACE_VTABLE)
        put(row, 8, struct.pack("<I", 1))
        put(row, 0x0c + index, bytes([1 << index]))
        result.append(row)
    for name, value in [("low", 1), ("upper-word", 0x100000000),
                        ("high-bit", 0x8000000000000000), ("all-bits", 0xffffffffffffffff)]:
        row = case("instance-" + name, fill="55")
        put(row, 0x10, struct.pack("<Q", value))
        result.append(row)
    for name, address in [("base", INTERFACE_VTABLE), ("context", CONTEXT_VTABLE), ("null", None)]:
        row = case("vtable-initial-" + name)
        if address is None:
            put(row, 0, bytes(8))
        else:
            put_vtable(row, address)
        result.append(row)
    row = case("derived-tail-preservation", fill="9d", size=TAIL_TEST_BYTES)
    put_vtable(row, INTERFACE_VTABLE)
    put(row, 8, struct.pack("<I", 1))
    put(row, 0x0c, bytes(4))
    put(row, 0x10, bytes(8))
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
    return result


def main():
    """Generate deterministic, uniquely named synthetic fixtures outside Git."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == 41 and len({row["id"] for row in generated}) == 41
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
