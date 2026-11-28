#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic linked-interface deleting-wrapper cases. Usage: SCRIPT OUT_DIR.

Both callees are scripted. No storage is really freed and no success of the
concrete cleanup on an uninitialized or null object is claimed. Objects have
at least the known 32-byte derived prefix. Void excludes incidental result bits.
"""
import copy
import json
from pathlib import Path
import sys


ADDRESS = "0x00fb6fc0"
CLEANUP = "0x00fb6fe0"
PREFIX_BYTES = 0x20
OWNED_BYTES = 0x100


def case(cid, object_name="object", offset=16, fill="a5", observe=True,
         cleanup_writes=(), delete_writes=(), cleanup_result="buf:spare+16",
         delete_result="0xfedcba9876543210", incoming=None, stack=()):
    """Observe complete pointers and bounded callback effects on owned storage."""
    assert object_name in ["object", "spare"]
    assert offset in [0, 8, 16, 64] and offset + PREFIX_BYTES <= OWNED_BYTES
    assert observe or not cleanup_writes and not delete_writes
    object_pointer = f"buf:{object_name}+{offset}"
    args = {"rdi": object_pointer, "rax": "0x1234", "rsi": "0x2345",
            "rdx": "0x3456", "rcx": "0x4567", "r8": "0x5678", "r9": "0x6789",
            "xmm0": "1f" * 16, "xmm1": "3e" * 16}
    assert not set(incoming or {}) - (set(args) - {"rdi"})
    args.update(incoming or {})

    def writes(changes):
        return [{"arg": 0, "offset": field, "bytes": data.hex()} for field, data in changes]

    cleanup = {"address": CLEANUP, "argc": 1, "argi": [0],
               "ret": cleanup_result, "writes": writes(cleanup_writes)}
    delete = {"name": "_ZdlPv", "argc": 1, "argi": [0],
              "ret": delete_result, "writes": writes(delete_writes)}
    if observe:
        cleanup["argmem"] = [{"arg": 0, "size": PREFIX_BYTES}]
        delete["argmem"] = [{"arg": 0, "size": PREFIX_BYTES}]
    buffers = {name: {"size": OWNED_BYTES,
                      "bytes": (fill if name == object_name else "d8") * OWNED_BYTES}
               for name in ["object", "spare"]}
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void", "args": args,
            "buffers": buffers, "memory": [],
            "stubs": [copy.deepcopy(cleanup) for _ in range(3)],
            "imports": [copy.deepcopy(delete) for _ in range(3)],
            "stack": list(stack) if stack else ["0x1234", "0x2345", "0x3456", "0x4567"]}


def cases():
    """Cover pointer preservation, call order, callback writes and ignored carriers."""
    for name in ["object", "spare"]:
        for offset in [0, 8, 16, 64]:
            for fill in ["00", "a5", "ff"]:
                yield case(f"storage-{name}-{offset}-{fill}", name, offset, fill)
    # Bad-pointer controls observe the argument without dereferencing it.
    yield case("full-pointer-arguments", observe=False)
    for role in ["cleanup", "delete"]:
        for field in [0, 8, 16, 24]:
            for fill in ["7f", "e3"]:
                changes = [(field, bytes.fromhex(fill * 8))]
                yield case(f"{role}-write-{field}-{fill}", **{role + "_writes": changes})
    yield case("ordered-callback-writes", cleanup_writes=[(0, b"\xaa" * 8)],
               delete_writes=[(0, b"\xbb" * 8)])
    for register in ["rax", "rsi", "rdx", "rcx", "r8", "r9"]:
        for index, value in enumerate(["0x0", "0xffffffffffffffff"]):
            yield case(f"ignored-{register}-{index}", incoming={register: value})
    for register in ["xmm0", "xmm1"]:
        for index, value in enumerate(["00" * 16, "ff" * 16]):
            yield case(f"ignored-{register}-{index}", incoming={register: value})
    for index, values in enumerate([["0x0"] * 4, ["0xffffffffffffffff"] * 4]):
        yield case(f"ignored-stack-{index}", stack=values)
    for role in ["cleanup", "delete"]:
        for index, value in enumerate(["0x0", "0x1", "0x100000000", "buf:spare+64"]):
            yield case(f"ignored-{role}-result-{index}", **{role + "_result": value})


def main():
    """Write deterministic, schema-ready inputs after complete uniqueness checks."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    inputs = [json.dumps({key: value for key, value in row.items() if key != "id"}, sort_keys=True)
              for row in generated]
    assert len(generated) == 68 and len(set(inputs)) == len(generated)
    assert len({row["id"] for row in generated}) == len(generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
