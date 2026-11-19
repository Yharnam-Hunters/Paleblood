#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic YEBIS allocation-count and free-dispatch cases. Usage: SCRIPT OUT_DIR.

Custom-method cases script the selected vtable method; default-free cases also
execute the original default method and script its free import. No storage is
actually freed. Allocation pointers remain opaque and need no interface layout.
"""
import copy
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6f50"
ALLOCATION_COUNT = "0x05866700"
DEFAULT_ALLOCATOR = "0x056b3610"
PROFILES = ["custom-a", "custom-b", "default-free"]
# Script identities, not inferred prototypes. All are distinct from the selected
# function and its executing default method.
CALLBACKS = ["0x00fb18b0", "0x00fb48d0", "0x00fb6da0", "0x00fb6fe0",
             "0x00fb6f20", "0x00fb6d40", "0x00fb4ac0"]
ALLOCATOR_BYTES = 0x80
ALLOCATION_BYTES = 0x40
TABLE_BYTES = 0x60


def pointer(name, offset=0):
    """Use the harness's named owned buffers, independent of their addresses."""
    return f"buf:{name}+{offset}"


def case(cid, profile="custom-a", count=2, neighbor=0x55aa55aa,
         allocation_offset=16, allocator_offset=16, table_offset=0,
         allocation_pointer=None, rewrite_count=None, method_result="0x0",
         fill="a5", allocation_is_allocator=False):
    """Supply a valid dispatch prefix and finite, observing scripted methods."""
    assert profile in PROFILES and 0 <= count <= 0xffffffff and 0 <= neighbor <= 0xffffffff
    assert allocator_offset % 8 == 0 and 0 <= allocator_offset <= ALLOCATOR_BYTES - 8
    assert table_offset % 8 == 0 and 0 <= table_offset <= TABLE_BYTES - 7 * 8
    assert 0 <= allocation_offset < ALLOCATION_BYTES
    assert rewrite_count is None or 0 <= rewrite_count <= 0xffffffff
    assert not allocation_is_allocator or (profile != "default-free" and allocation_pointer is None)
    assert allocation_pointer is None or (rewrite_count is None and not allocation_is_allocator)
    table = "table-b" if profile == "custom-b" else "table-a"
    allocator = (f"guest:{DEFAULT_ALLOCATOR}" if profile == "default-free"
                 else pointer("allocator", allocator_offset))
    allocation = allocation_pointer or pointer("allocation", allocation_offset)
    if allocation_is_allocator:
        allocation = pointer("allocator", allocator_offset)
    if rewrite_count is not None:
        # A custom method may write through its opaque pointer. This profile
        # points it at the writable count, observing the decrement before that
        # callback replaces the value. It never aliases an allocator vptr.
        allocation = f"guest:{ALLOCATION_COUNT}"

    memory = [{"addr": ALLOCATION_COUNT,
               "bytes": struct.pack("<II", count, neighbor).hex() + "c7" * 8},
              {"addr": pointer("allocator", allocator_offset),
               "pointer": f"{table}+{table_offset}"}]
    for table_name in ["table-a", "table-b"]:
        methods = list(CALLBACKS)
        if table_name == "table-b":
            methods[4], methods[5] = methods[5], methods[4]
        for slot, callback in enumerate(methods):
            memory.append({"addr": pointer(table_name, table_offset + slot * 8),
                           "guest": callback})

    custom_reads = [] if allocation_pointer is not None else [{"arg": 0, "size": 8}]
    free_reads = []
    if rewrite_count is not None:
        custom_reads.append({"arg": 1, "size": 4})
        free_reads.append({"arg": 0, "size": 4})
    elif allocation_pointer is None:
        remaining = (ALLOCATOR_BYTES - allocator_offset if allocation_is_allocator
                     else ALLOCATION_BYTES - allocation_offset)
        custom_reads.append({"arg": 1, "size": min(16, remaining)})
        free_reads.append({"arg": 0, "size": min(16, remaining)})
    custom_writes = [] if rewrite_count is None else [
        {"arg": 1, "bytes": struct.pack("<I", rewrite_count).hex()}]
    import_writes = [] if rewrite_count is None else [
        {"addr": ALLOCATION_COUNT, "bytes": struct.pack("<I", rewrite_count).hex()}]
    stubs = []
    for callback in CALLBACKS:
        entry = {"address": callback, "argc": 2, "argi": [0, 1],
                 "argmem": custom_reads, "ret": method_result, "writes": custom_writes}
        stubs.extend(copy.deepcopy(entry) for _ in range(2))
    imported_free = {"name": "free", "argc": 1, "argi": [0], "argmem": free_reads,
                     "ret": method_result, "writes": import_writes}
    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
              "args": {"rdi": allocator, "rsi": allocation, "rax": "0xfedcba9876543210",
                       "rdx": "0xffffffffffffffff", "rcx": "0x100000000",
                       "r8": "0x8000000000000000", "r9": "0x1"},
              "buffers": {"allocator": {"size": ALLOCATOR_BYTES, "bytes": "b6" * ALLOCATOR_BYTES},
                          "allocation": {"size": ALLOCATION_BYTES, "bytes": fill * ALLOCATION_BYTES},
                          "table-a": {"size": TABLE_BYTES}, "table-b": {"size": TABLE_BYTES}},
              "memory": memory, "stubs": stubs,
              "imports": [copy.deepcopy(imported_free) for _ in range(2)]}
    assert all(entry["address"] != ADDRESS for entry in stubs)
    return result


def cases():
    """Cover wrapping DWORD updates, the exact method slot and full pointers."""
    result = []
    for profile in PROFILES:
        for count in [0, 1, 0x100, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-count-{count:08x}", profile, count=count))
        for value in [0, 1, 0xffffffff]:
            result.append(case(f"{profile}-callback-count-{value:08x}", profile,
                               count=0x10000, rewrite_count=value))
        for index, value in enumerate(["0x0", "0x1", "0x100000000", "0xffffffffffffffff"]):
            result.append(case(f"{profile}-opaque-pointer-{index}", profile, allocation_pointer=value))
        for offset in [0, 1, 7, 16, 63]:
            result.append(case(f"{profile}-allocation-offset-{offset}", profile, allocation_offset=offset))
        for value in ["0x1", "0x100000000", "0xffffffffffffffff"]:
            result.append(case(f"{profile}-ignored-result-{value[2:]}", profile, method_result=value))
        for value in [0, 0xffffffff]:
            result.append(case(f"{profile}-neighbor-{value:08x}", profile, count=0, neighbor=value))
        for fill in ["00", "ff"]:
            result.append(case(f"{profile}-payload-{fill}", profile, fill=fill))
    for profile in ["custom-a", "custom-b"]:
        for offset in [0, 8, 24, 56]:
            result.append(case(f"{profile}-allocator-offset-{offset}", profile, allocator_offset=offset))
        for offset in [0, 24]:
            result.append(case(f"{profile}-same-pointer-{offset}", profile, allocator_offset=offset,
                               allocation_is_allocator=True))
    for offset in [8, 24]:
        result.append(case(f"table-prefix-offset-{offset}", table_offset=offset))
    return result


def main():
    """Write deterministic synthetic fixtures with unique hyphen-only names."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == 92 and len({row["id"] for row in generated}) == len(generated)
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
