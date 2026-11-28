#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic linked-interface cleanup cases. Usage: SCRIPT OUT_DIR.

Logger, assertion, reference, free and base-destructor boundaries are scripted.
Fixed callback writes affect declared owned buffers or writable original globals;
readonly tables are never changed and no storage is actually freed. The missing
cached-pointer assertion cannot return normally, so that fault is not a passing
case. These are unit protocol fixtures, not naturally allocated game objects.
"""
import copy
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from harness import EBOOT_BASE, IMAGE, PAGE, SANDBOX

ADDRESS = "0x00fb6fe0"
LOG = "0x00fb1a90"
FREE = "0x00fb6f50"
BASE = "0x00fb6da0"
REFERENCE_CALLBACKS = ["0x00fb18b0", "0x00fb48d0", "0x00fb6fc0"]
ROOT_GATE = "0x05519a68"
DETAIL_GATE = "0x05519a70"
CLEANUP_GATE = "0x05865eb0"
ALLOCATOR = "0x056b3618"


def pointer(name, offset=0):
    """Use the harness's named owned-buffer references."""
    return f"buf:{name}+{offset}"


def word(address, value, width=4):
    """Describe one fixed, bounded callback write."""
    return {"addr": address, "bytes": int(value).to_bytes(width, "little").hex()}


def pointer_write(address, value):
    """Defer an owned/global pointer encoding until the buffer layout is known."""
    return {"addr": address, "value_pointer": value}


def case(cid, count=2, instance="a", other="a", flag=0,
         root_gate=0, detail_gate=0, cleanup_gate=0, offset=16, fill="a5",
         count_neighbor=0xc7c7c7c7, gate_neighbor=0xb6b6b6b6,
         detail_neighbor=0xd8d8d8d8, cleanup_neighbor=0xe9e9e9e9,
         log_writes=(), assert_writes=(), virtual_writes=(), free_writes=(),
         base_writes=(), observe_memory=True, observe_lines=True,
         assert_count_snapshot=False,
         carriers="0x13579bdf2468ace0", incoming=None, stack=()):
    """Build valid storage, distinct callback identities and typed call views."""
    assert instance in (None, "a", "b", "self", "other")
    assert other in (None, "a", "b", "self") and offset % 8 == 0
    assert 0 <= offset <= 128 and 0 <= flag <= 255
    assert all(0 <= v <= 0xffffffff for v in
               [count, root_gate, detail_gate, cleanup_gate, count_neighbor,
                gate_neighbor, detail_neighbor, cleanup_neighbor])
    assert instance is None or other is not None
    assert not assert_count_snapshot or (instance == "a" and count == 0 and flag == 0 and root_gate == 0)
    buffers = {"allocators": {"size": 128, "bytes": "e9" * 128},
               "flags": {"size": 128, "bytes": "d8" * 128},
               "object": {"size": 256, "bytes": fill * 256},
               "other": {"size": 256, "bytes": "b6" * 256},
               "references": {"size": 256, "bytes": "c7" * 256},
               "tables": {"size": 256}}
    starts = {}
    cursor = SANDBOX
    for name in sorted(buffers):
        starts[name] = cursor
        cursor += (max(1, (buffers[name]["size"] + PAGE - 1) // PAGE) + 1) * PAGE

    def value(reference):
        if reference is None:
            return 0
        if reference.startswith("buf:"):
            name, _, off = reference[4:].partition("+")
            return starts[name] + int(off or "0", 0)
        if reference.startswith("guest:"):
            return IMAGE + int(reference[6:], 0) - EBOOT_BASE
        return int(reference, 0)

    def changes(items):
        result = []
        for item in items:
            row = copy.deepcopy(item)
            if "value_pointer" in row:
                row["bytes"] = struct.pack("<Q", value(row.pop("value_pointer"))).hex()
            result.append(row)
        return result

    self_pointer = pointer("object", offset)
    reference = {None: None, "a": pointer("references", 16),
                 "b": pointer("references", 96), "self": self_pointer,
                 "other": pointer("other", 16)}[instance]
    other_pointer = {None: None, "a": pointer("other", 16),
                     "b": pointer("other", 96), "self": self_pointer}[other]
    memory = []

    def put(address, data):
        memory.append({"addr": address, "bytes": data.hex()})

    def put_pointer(address, target):
        put(address, struct.pack("<Q", value(target)))

    put_pointer(self_pointer, pointer("tables", 64))
    put(pointer("object", offset + 8), struct.pack("<I", 0))
    put_pointer(pointer("object", offset + 16), reference)
    put_pointer(pointer("object", offset + 24), other_pointer)
    for other_offset, flag_offset in [(16, 16), (96, 64)]:
        put_pointer(pointer("other", other_offset), pointer("tables"))
        put(pointer("other", other_offset + 8), struct.pack("<I", 17))
        put_pointer(pointer("other", other_offset + 16), pointer("flags", flag_offset))
    put(pointer("flags", 16), bytes([flag]))
    put(pointer("flags", 64), bytes([0]))
    for reference_offset, table_offset in [(16, 0), (96, 64)]:
        put_pointer(pointer("references", reference_offset), pointer("tables", table_offset))
        put(pointer("references", reference_offset + 8), struct.pack("<II", count, count_neighbor))
    if instance in ("self", "other"):
        put(pointer("object", offset + 8) if instance == "self" else pointer("other", 24),
            struct.pack("<II", count, count_neighbor))
    for table_offset, callback in [(0, REFERENCE_CALLBACKS[0]), (64, REFERENCE_CALLBACKS[1]),
                                   (128, REFERENCE_CALLBACKS[0]), (192, REFERENCE_CALLBACKS[1])]:
        for slot in range(6):
            target = callback if slot == 4 else REFERENCE_CALLBACKS[2]
            memory.append({"addr": pointer("tables", table_offset + slot * 8), "guest": target})
    for allocator_offset, table_offset in [(16, 128), (64, 192)]:
        put_pointer(pointer("allocators", allocator_offset), pointer("tables", table_offset))
    for address, v, neighbor in [(ROOT_GATE, root_gate, gate_neighbor),
                                 (DETAIL_GATE, detail_gate, detail_neighbor),
                                 (CLEANUP_GATE, cleanup_gate, cleanup_neighbor)]:
        put(address, struct.pack("<II", v, neighbor))
    put_pointer(ALLOCATOR, pointer("allocators", 16))

    stubs = []
    for index in range(12):
        entry = {"address": LOG, "argc": 2 if observe_lines and index % 2 == 0 else 1,
                 "argi": [0], "ret": carriers,
                 "writes": changes(log_writes[index] if index < len(log_writes) else ())}
        if observe_lines and index % 2 == 0:
            entry["argu32"] = [1]
        stubs.append(entry)
    for address in REFERENCE_CALLBACKS:
        entry = {"address": address, "argc": 1, "argi": [0], "ret": carriers,
                 "writes": changes(virtual_writes)}
        if observe_memory:
            entry["argmem"] = [{"arg": 0, "size": 12}]
        stubs.extend(copy.deepcopy(entry) for _ in range(4))
    free = {"address": FREE, "argc": 2, "argi": [0, 1], "ret": carriers,
            "writes": changes(free_writes)}
    base = {"address": BASE, "argc": 1, "argi": [0], "ret": carriers,
            "writes": changes(base_writes)}
    if observe_memory:
        free["argmem"] = [{"arg": 0, "size": 8}, {"arg": 1, "size": 12}]
        base["argmem"] = [{"arg": 0, "size": 32}]
    stubs.extend(copy.deepcopy(free) for _ in range(4))
    stubs.extend(copy.deepcopy(base) for _ in range(4))
    imports = []
    for index in range(8):
        entry = {"name": "_Assert", "argc": 2, "argi": [0, 1], "ret": carriers,
                 "writes": changes(assert_writes[index] if index < len(assert_writes) else ())}
        if assert_count_snapshot and index == 0:
            # This case's only assertion uses the count message. Observe the
            # already-decremented DWORD before its scripted returning write.
            entry["argmem"] = [{"arg": 0, "size": 4,
                                "offset": value(reference) + 8 - (IMAGE + 0x04bff673 - EBOOT_BASE)}]
        imports.append(entry)
    args = {"rdi": self_pointer, "rax": "0xabcdef0123456789", "rsi": "0x123456789abcdef0",
            "rdx": "0x100000000", "rcx": "0x8000000000000000",
            "r8": "0xffffffffffffffff", "r9": "0x1"}
    args.update(incoming or {})
    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void", "args": args,
              "buffers": buffers, "memory": memory, "stubs": stubs, "imports": imports}
    if stack:
        result["stack"] = list(stack)
    return result


def cases():
    """Vary complete widths, callback writes, aliases and every ordinary branch."""
    result = [case("empty-no-other", instance=None, other=None),
              case("empty-other-a", instance=None),
              case("empty-other-self", instance=None, other="self")]
    for count in [0, 2, 0xff, 0x100, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff]:
        result.append(case(f"count-{count:08x}", count=count))
    for flag in [0, 1]:
        for root in [0, 1]:
            for detail in [0, 1]:
                for cleanup in [0, 1]:
                    result.append(case(f"gates-{flag}-{root}-{detail}-{cleanup}", count=1, flag=flag,
                                       root_gate=root, detail_gate=detail, cleanup_gate=cleanup))
    for flag in [2, 0x80, 0xff]:
        result.append(case(f"flag-{flag:02x}", flag=flag, count=1, root_gate=1,
                           detail_gate=1, cleanup_gate=1))
    for field in ["root_gate", "detail_gate", "cleanup_gate"]:
        for value in [0x100, 0x10000, 0x80000000, 0xffffffff]:
            kwargs = {"root_gate": 1, "detail_gate": 1, "cleanup_gate": 1, field: value}
            result.append(case(f"wide-{field}-{value:08x}", flag=1, count=1, **kwargs))
    for field in ["root_gate", "detail_gate", "cleanup_gate"]:
        kwargs = {"root_gate": 1, "detail_gate": 1, "cleanup_gate": 1, field: 0}
        neighbor = {"root_gate": "gate_neighbor", "detail_gate": "detail_neighbor",
                    "cleanup_gate": "cleanup_neighbor"}[field]
        kwargs[neighbor] = 0
        result.append(case("zero-neighbor-" + field, flag=1, count=1, **kwargs))
    for count in [0, 1, 0x80000000]:
        for neighbor in [0, 0xffffffff]:
            result.append(case(f"count-neighbor-{count:08x}-{neighbor:08x}", count=count,
                               count_neighbor=neighbor))
    for before in [0, 0x80000000]:
        for after in [0, 1, 2, 0x80000000, 0xffffffff]:
            result.append(case(f"assert-count-{before:08x}-{after:08x}", count=before,
                               assert_writes=[[word(pointer("references", 24), after)]]))
    result.append(case("busy-assert-clears-instance", count=1, flag=1,
                       assert_writes=[[pointer_write(pointer("object", 32), None)]]))
    result.append(case("busy-assert-replaces-instance", count=2, flag=1,
                       assert_writes=[[pointer_write(pointer("object", 32), pointer("references", 96)),
                                       word(pointer("references", 104), 1)]]))
    result.append(case("count-assert-keeps-cached-reference", count=0,
                       assert_writes=[[pointer_write(pointer("object", 32), pointer("references", 96)),
                                       word(pointer("references", 24), 0)]]))
    result.append(case("count-assert-observes-decrement", count=0, assert_count_snapshot=True,
                       assert_writes=[[word(pointer("references", 24), 3)]]))
    logging = {"count": 1, "flag": 1, "root_gate": 1, "detail_gate": 1, "cleanup_gate": 1}
    scripted = [
        ("entry-first-clears-instance", 0, [pointer_write(pointer("object", 32), None)]),
        ("entry-second-replaces-other", 1, [pointer_write(pointer("object", 40), pointer("other", 96))]),
        ("entry-second-replaces-allocator", 1, [pointer_write(ALLOCATOR, pointer("allocators", 64))]),
        ("entry-second-replaces-other-instance", 1,
         [pointer_write(pointer("other", 32), pointer("flags", 64))]),
        ("warning-first-replaces-other-instance", 2,
         [pointer_write(pointer("other", 32), pointer("flags", 64))]),
        ("warning-first-clears-byte", 2, [word(pointer("flags", 16), 0, 1)]),
        ("warning-second-clears-byte", 3, [word(pointer("flags", 16), 0, 1)]),
        ("entry-first-clears-root", 0, [word(ROOT_GATE, 0)]),
        ("entry-first-clears-cleanup-gate", 0, [word(CLEANUP_GATE, 0)]),
        ("warning-first-clears-root", 2, [word(ROOT_GATE, 0)]),
        ("warning-first-clears-detail", 2, [word(DETAIL_GATE, 0)]),
        ("warning-second-replaces-instance", 3,
         [pointer_write(pointer("object", 32), pointer("references", 96)),
          word(pointer("references", 104), 1)]),
        ("entry-first-keeps-vtable-write", 0,
         [pointer_write(pointer("object", 16), pointer("tables", 64))]),
        ("exit-first-keeps-vtable-write", 4,
         [pointer_write(pointer("object", 16), pointer("tables", 64))]),
        ("exit-second-keeps-instance-write", 5,
         [pointer_write(pointer("object", 32), pointer("references", 96))]),
    ]
    for name, index, writes in scripted:
        callbacks = [[] for _ in range(index + 1)]
        callbacks[index] = writes
        result.append(case(name, log_writes=callbacks, **logging))
    result.append(case("virtual-replaces-allocator-and-instance", count=1,
                       virtual_writes=[pointer_write(ALLOCATOR, pointer("allocators", 64)),
                                       pointer_write(pointer("object", 32), pointer("references", 96))]))
    result.append(case("virtual-changes-count", count=1,
                       virtual_writes=[word(pointer("references", 24), 77)]))
    result.append(case("free-replaces-instance", count=1,
                       free_writes=[pointer_write(pointer("object", 32), pointer("references", 96))]))
    result.append(case("base-keeps-instance-write", count=1,
                       base_writes=[pointer_write(pointer("object", 32), pointer("references", 96))]))
    for offset in [0, 8, 64]:
        for fill in ["00", "ff"]:
            result.append(case(f"storage-{offset}-{fill}", count=1, offset=offset, fill=fill))
    result.append(case("other-aliases-self", count=2, other="self"))
    result.append(case("reference-is-other", count=1, instance="other"))
    for count in [2, 3]:
        result.append(case(f"reference-aliases-self-{count}", count=count, instance="self"))
    result.append(case("pointer-only-boundaries", count=1, flag=1, root_gate=1,
                       detail_gate=1, cleanup_gate=1, observe_memory=False, observe_lines=False))
    for value in ["0x0", "0xffffffff", "0x100000000"]:
        result.append(case("ignored-carriers-" + value[2:], count=1, carriers=value))
    for register in ["rax", "rsi", "rdx", "rcx", "r8", "r9"]:
        for name, value in [("zero", "0x0"), ("poison", "0x13579bdf2468ace0")]:
            result.append(case(f"ignored-{register}-{name}", count=1, incoming={register: value}))
    for register in ["xmm0", "xmm1"]:
        result.append(case("ignored-" + register, count=1, incoming={register: "0100807f" * 4}))
    result.append(case("ignored-stack-zero", count=1, stack=["0x0"] * 4))
    result.append(case("ignored-stack-poison", count=1,
                       stack=["0xffffffffffffffff", "0x100000000", "buf:object+16", "0x8000000000000000"]))
    return result


def main():
    """Write deterministic fixtures and reject duplicate IDs or complete inputs."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == len({r["id"] for r in generated})
    payloads = [json.dumps({k: v for k, v in r.items() if k != "id"}, sort_keys=True) for r in generated]
    assert len(payloads) == len(set(payloads))
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
