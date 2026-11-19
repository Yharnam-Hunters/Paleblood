#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic interface cleanup/delete cases. Usage: SCRIPT OUT_DIR.

All objects are owned. Returning assertion and scalar-delete imports are
scripted; no storage is actually freed. A void result excludes incidental
return-register contents. Original image tables and strings remain read-only.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6d40"
INTERFACE_VTABLE = "0x056b35e0"
CONTEXT_VTABLE = "0x056b3580"
PREFIX_BYTES = 0x18
OWNED_BYTES = 0x80


def case(cid, count=0, instance=0, offset=16, fill="a5", size=OWNED_BYTES,
         first_writes=(), second_writes=(), delete_writes=(),
         assertion_result="0xfedcba9876543210", delete_result="0x123456789abcdef0",
         incoming=None, stack=()):
    """Supply valid objects and observing imports on every returning path."""
    assert 0 <= count <= 0xffffffff and 0 <= instance <= 0xffffffffffffffff
    assert offset >= 0 and offset % 8 == 0 and offset + PREFIX_BYTES <= size
    self_pointer = f"buf:object+{offset}"

    def writes(changes):
        return [{"addr": f"buf:object+{offset + field}", "bytes": data.hex()}
                for field, data in changes]

    args = {"rdi": self_pointer, "rsi": "buf:spare+16",
            "rax": "0xfedcba9876543210", "rdx": "0xffffffffffffffff",
            "rcx": "0x100000000", "r8": "0x8000000000000000", "r9": "0x1"}
    args.update(incoming or {})
    imports = []
    for changes in [first_writes, second_writes, ()]:
        imports.append({"name": "_Assert", "argc": 2, "argi": [0, 1],
                        "ret": assertion_result, "writes": writes(changes)})
    for _ in range(3):
        imports.append({"name": "_ZdlPv", "argc": 1, "argi": [0],
                        "argmem": [{"arg": 0, "size": PREFIX_BYTES}],
                        "ret": delete_result, "writes": writes(delete_writes)})
    row = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
           "args": args,
           "buffers": {"object": {"size": size, "bytes": fill * size},
                       "spare": {"size": OWNED_BYTES, "bytes": "d7" * OWNED_BYTES}},
           "memory": [{"addr": self_pointer, "guest": CONTEXT_VTABLE},
                      {"addr": f"buf:object+{offset + 8}",
                       "bytes": struct.pack("<I", count).hex()},
                      {"addr": f"buf:object+{offset + 0x10}",
                       "bytes": struct.pack("<Q", instance).hex()}],
           "imports": imports, "stubs": []}
    if stack:
        row["stack"] = list(stack)
    return row


def cases():
    """Distinguish field widths, assertion order, publication and same-object delete."""
    result = []
    for count in [0, 1, 2, 0xff, 0x100, 0xffff, 0x10000, 0x80000000, 0xffffffff]:
        for instance in [0, 1, 0x100000000, 0x8000000000000000, 0xffffffffffffffff]:
            result.append(case(f"fields-{count:08x}-{instance:016x}", count, instance))
    for offset in [0, 8, 24, 64]:
        for fill in ["00", "ff"]:
            for count, instance in [(0, 0), (1, 0x100000000)]:
                result.append(case(f"storage-{offset}-{fill}-{count}", count, instance,
                                   offset=offset, fill=fill))
    for fill in ["00", "ff"]:
        for count in [0, 1]:
            for instance in [0, 1]:
                result.append(case(f"exact-prefix-{fill}-{count}-{instance}", count, instance,
                                   offset=0, size=PREFIX_BYTES, fill=fill))
    for index in range(4):
        row = case(f"count-neighbor-{index}", fill="00")
        row["memory"].append({"addr": f"buf:object+{16 + 0x0c + index}",
                              "bytes": bytes([1 << index]).hex()})
        result.append(row)
    for name, value in [("base", INTERFACE_VTABLE), ("null", "0x0")]:
        row = case("incoming-vtable-" + name)
        row["memory"][0] = ({"addr": row["args"]["rdi"], "bytes": "00" * 8}
                            if value == "0x0" else
                            {"addr": row["args"]["rdi"], "guest": value})
        result.append(row)
    for name, before, after in [("create-low", 0, 1), ("create-high", 0, 0x100000000),
                                ("clear", 1, 0), ("keep-high", 1, 0x8000000000000000)]:
        result.append(case("assert-instance-" + name, 1, before,
                           first_writes=[(0x10, struct.pack("<Q", after))]))
    for count in [0, 2, 0x80000000, 0xffffffff]:
        result.append(case(f"assert-count-{count:08x}", 1,
                           first_writes=[(8, struct.pack("<I", count))]))
    # A returning assertion may overwrite the vtable published before it.
    marker = struct.pack("<Q", 0x13579bdf2468ace0)
    result.append(case("first-assert-vtable-write", 1, first_writes=[(0, marker)]))
    result.append(case("second-assert-vtable-write", 1, 1, second_writes=[(0, marker)]))
    for field, data in [(0, marker), (8, struct.pack("<I", 0x56789abc)),
                        (0x10, struct.pack("<Q", 0x8000000012345678))]:
        result.append(case(f"delete-write-{field}", delete_writes=[(field, data)]))
    # Observe the complete argument without dereferencing the callee's pointer.
    row = case("delete-full-pointer-argument")
    for entry in row["imports"]:
        if entry["name"] == "_ZdlPv":
            del entry["argmem"]
    result.append(row)
    for value in ["0x0", "0x1", "0xff", "0x100000000", "0xffffffffffffffff"]:
        result.append(case("ignored-rsi-" + value[2:], incoming={"rsi": value}))
    for register in ["rax", "rdx", "rcx", "r8", "r9"]:
        values = ["0x0", "0x1"] if register == "rdx" else ["0x0", "0xffffffffffffffff"]
        for value in values:
            result.append(case(f"ignored-{register}-{value[2:]}",
                               incoming={register: value}))
    for name, values in [("zero", ["0x0"] * 4),
                         ("poison", ["0xffffffffffffffff", "0x100000000",
                                     "buf:spare+16", "0x8000000000000000"])]:
        result.append(case("ignored-stack-" + name, stack=values))
    for name, value in [("finite", "0000803f000000400000404000008040"),
                        ("nan", "0100807f" * 4)]:
        result.append(case("ignored-xmm-" + name, incoming={"xmm0": value, "xmm1": value}))
    for value in ["0x0", "0x1", "0x100000000", "0xffffffffffffffff"]:
        result.append(case("ignored-assert-result-" + value[2:], 1, 1,
                           assertion_result=value))
        result.append(case("ignored-delete-result-" + value[2:], delete_result=value))
    return result


def main():
    """Write deterministic, uniquely named synthetic fixtures outside Git."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len({row["id"] for row in generated}) == len(generated)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
