#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic YEBIS allocation dispatch and counter cases. Usage: SCRIPT OUT_DIR.

Custom profiles script an observing allocation method. The default profile runs
the original default method and scripts its malloc import. No real allocation
occurs; returned pointers are opaque and may be null.
"""
import copy
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6f20"
ALLOCATION_COUNT = "0x05866700"
DEFAULT_ALLOCATOR = "0x056b3610"
PROFILES = ["custom-a", "custom-b", "default-allocate"]
# Distinct script identities, separate from the selected and default functions.
CALLBACKS = ["0x00fb18b0", "0x00fb48d0", "0x00fb6da0", "0x00fb6fe0",
             "0x00fb6f50", "0x00fb6d40", "0x00fb4ac0"]
ALLOCATOR_BYTES = 0x80
TABLE_BYTES = 0x60
METADATA_BYTES = 0x40
RETURNED_BYTES = 0x40


def pointer(name, offset=0):
    """Refer to owned storage without reproducing the harness address layout."""
    return f"buf:{name}+{offset}"


def case(cid, profile="custom-a", count=2, neighbor=0x55aa55aa,
         size="0x178", source_line=629, source_file=None, allocation_name=None,
         result=None, callback_count=None, allocator_offset=16, table_offset=0):
    """Supply a bounded allocator prefix and finite, observing scripted calls."""
    assert profile in PROFILES
    assert 0 <= count <= 0xffffffff and 0 <= neighbor <= 0xffffffff
    assert 0 <= source_line <= 0xffffffff
    assert callback_count is None or 0 <= callback_count <= 0xffffffff
    assert allocator_offset % 8 == 0 and 0 <= allocator_offset <= ALLOCATOR_BYTES - 8
    assert table_offset % 8 == 0 and 0 <= table_offset <= TABLE_BYTES - len(CALLBACKS) * 8
    allocator = (f"guest:{DEFAULT_ALLOCATOR}" if profile == "default-allocate"
                 else pointer("allocator", allocator_offset))
    source_file = source_file if source_file is not None else pointer("source-file", 8)
    allocation_name = allocation_name if allocation_name is not None else pointer("allocation-name", 16)
    result = result if result is not None else pointer("returned", 16)
    if callback_count is not None and profile != "default-allocate":
        # This custom method treats the forwarded metadata pointer as opaque
        # writable storage, so its count update uses the proven argument view.
        source_file = f"guest:{ALLOCATION_COUNT}"
    table = "table-b" if profile == "custom-b" else "table-a"
    memory = [{"addr": ALLOCATION_COUNT,
               "bytes": struct.pack("<II", count, neighbor).hex() + "c7" * 8},
              {"addr": pointer("allocator", allocator_offset),
               "pointer": f"{table}+{table_offset}"}]
    for table_name in ["table-a", "table-b"]:
        methods = list(CALLBACKS)
        if table_name == "table-b":
            methods[3], methods[4] = methods[4], methods[3]
        for slot, callback in enumerate(methods):
            memory.append({"addr": pointer(table_name, table_offset + slot * 8),
                           "guest": callback})
    import_writes = [] if callback_count is None else [
        {"addr": ALLOCATION_COUNT, "bytes": struct.pack("<I", callback_count).hex()}]
    custom_writes = [] if callback_count is None else [
        {"arg": 2, "bytes": struct.pack("<I", callback_count).hex()}]
    custom_reads = [{"arg": 0, "size": 8}]
    if callback_count is not None:
        custom_reads.append({"arg": 2, "size": 4})
    stubs = []
    for callback in CALLBACKS:
        # The known callers define the source line as a DWORD. Other arguments
        # are full-width values; ambient upper source-line bits are not observed.
        entry = {"address": callback, "argc": 5, "argi": [0, 1, 2, 4], "argu32": [3],
                 "argmem": custom_reads, "ret": result, "writes": custom_writes}
        stubs.extend(copy.deepcopy(entry) for _ in range(2))
    imported_malloc = {"name": "malloc", "argc": 1, "argi": [0],
                       "ret": result, "writes": import_writes}
    generated = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i64",
                 "args": {"rdi": allocator, "rsi": size, "rdx": source_file,
                          "rcx": hex(source_line), "r8": allocation_name,
                          "rax": "0xfedcba9876543210", "r9": "0x8000000000000000"},
                 "buffers": {
                     "allocator": {"size": ALLOCATOR_BYTES, "bytes": "b6" * ALLOCATOR_BYTES},
                     "table-a": {"size": TABLE_BYTES}, "table-b": {"size": TABLE_BYTES},
                     "source-file": {"size": METADATA_BYTES, "bytes": "63" * (METADATA_BYTES - 1) + "00"},
                     "allocation-name": {"size": METADATA_BYTES, "bytes": "6e" * (METADATA_BYTES - 1) + "00"},
                     "returned": {"size": RETURNED_BYTES, "bytes": "a5" * RETURNED_BYTES}},
                 "memory": memory, "stubs": stubs,
                 "imports": [copy.deepcopy(imported_malloc) for _ in range(2)]}
    assert all(row["address"] != ADDRESS for row in stubs)
    return generated


def cases():
    """Cover post-call DWORD updates, callback writes and full pointer results."""
    generated = []
    for profile in PROFILES:
        for value in [0, 1, 0xff, 0xffff, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]:
            generated.append(case(f"{profile}-count-{value:08x}", profile, count=value))
        for value in [0, 1, 0x80000000, 0xffffffff]:
            generated.append(case(f"{profile}-callback-count-{value:08x}", profile,
                                  count=0x10000, callback_count=value))
        for index, value in enumerate(["0x0", "0x1", "0x100000000", "0x8000000000000000",
                                       "0xffffffffffffffff", pointer("returned", 0),
                                       pointer("returned", 1), pointer("returned", 63),
                                       pointer("allocator", 16), f"guest:{ALLOCATION_COUNT}"]):
            generated.append(case(f"{profile}-result-{index}", profile, result=value))
        for index, value in enumerate(["0x0", "0x1", "0x7", "0x8", "0xff", "0xffffffff",
                                       "0x100000000", "0x8000000000000000", "0xffffffffffffffff"]):
            generated.append(case(f"{profile}-size-{index}", profile, size=value))
        for value in [0, 0xffffffff]:
            generated.append(case(f"{profile}-neighbor-{value:08x}", profile,
                                  count=0xffffffff, neighbor=value))
    for profile in ["custom-a", "custom-b"]:
        for value in [0, 1, 0x7fffffff, 0x80000000, 0xffffffff]:
            generated.append(case(f"{profile}-source-line-{value:08x}", profile, source_line=value))
        for role in ["file", "name"]:
            for index, value in enumerate(["0x0", "0x100000000", "0xffffffffffffffff"]):
                kwargs = {"source_file" if role == "file" else "allocation_name": value}
                generated.append(case(f"{profile}-{role}-pointer-{index}", profile, **kwargs))
        for offset in [0, 8, 24, 56]:
            generated.append(case(f"{profile}-allocator-offset-{offset}", profile,
                                  allocator_offset=offset))
        for offset in [8, 24]:
            generated.append(case(f"{profile}-table-offset-{offset}", profile, table_offset=offset))
        generated.append(case(f"{profile}-metadata-same-pointer", profile,
                              source_file=pointer("source-file", 8),
                              allocation_name=pointer("source-file", 8)))
        generated.append(case(f"{profile}-metadata-is-allocator", profile,
                              source_file=pointer("allocator", 16),
                              allocation_name=pointer("allocator", 16)))
    return generated


def main():
    """Write deterministic fixtures with unique hyphen-only filenames."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == 137 and len({row["id"] for row in generated}) == len(generated)
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
