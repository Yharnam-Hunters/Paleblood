#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic IPfxInterface::Release cases. Usage: SCRIPT OUT_DIR.

Protocol cases script cleanup and the free wrapper. Allocator-dispatch cases
execute the original wrapper with owned allocator tables; default-free cases
also execute its default method, scripting only imported free. No storage is
actually freed. The allocator-reload cases deliberately alias the writable
allocator global with an object's instance field; they are synthetic aliases,
not naturally allocated YEBIS objects. Original readonly tables stay untouched.
"""
import copy
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6c80"
FREE_WRAPPER = "0x00fb6f50"
CLEANUP = "0x00fb6da0"
OTHER_CLEANUPS = ["0x00fb18b0", "0x00fb48d0", "0x00fb6fe0"]
# These entry points are replaced by scripted callbacks in owned tables. Their
# use as call identities does not assert those originals' function prototypes.
ALLOCATOR_CALLBACK_A = "0x00fb6ce0"
ALLOCATOR_CALLBACK_B = "0x00fb6f20"
ALLOCATOR_GLOBAL = "0x056b3618"
ALLOCATION_COUNT = "0x05866700"
DEFAULT_ALLOCATOR = "0x056b3610"
OBJECT_ALIAS = "0x056b3608"
PREFIX_BYTES = 0x18
OWNED_BYTES = 0x80
PROFILES = ["protocol", "allocator-dispatch", "default-free"]


def pointer(name, offset=0):
    """Refer to owned storage without duplicating the harness address layout."""
    return f"buf:{name}+{offset}"


def case(cid, count=2, profile="protocol", offset=16, fill="a5", size=OWNED_BYTES,
         assert_count=None, cleanup_count=None, free_count=None, alias=False,
         allocator_offset=16, allocation_count=0, free_allocation_count=None,
         cleanup_instance=False, cleanup_result="0x0", free_result="0x0",
         incoming=None, stack=()):
    """Provide valid prefixes, observing callbacks and bounded callback writes."""
    assert profile in PROFILES and offset >= 0 and offset % 8 == 0
    assert offset + PREFIX_BYTES <= size and allocator_offset % 8 == 0
    assert 0 <= allocator_offset <= OWNED_BYTES - 8
    assert all(value is None or 0 <= value <= 0xffffffff for value in
               [count, assert_count, cleanup_count, free_count, allocation_count,
                free_allocation_count])
    assert not alias or (profile != "default-free" and not cleanup_instance)
    assert free_allocation_count is None or profile == "default-free"

    def object_address(field=0):
        if alias:
            return f"0x{int(OBJECT_ALIAS, 16) + field:08x}"
        return pointer("object", offset + field)

    self_pointer = f"guest:{OBJECT_ALIAS}" if alias else pointer("object", offset)
    args = {"rdi": self_pointer, "rsi": pointer("spare", 16),
            "rax": "0xfedcba9876543210", "rdx": "0xffffffffffffffff",
            "rcx": "0x100000000", "r8": "0x8000000000000000", "r9": "0x1"}
    assert not set(incoming or {}) - {"rax", "rsi", "rdx", "rcx", "r8", "r9", "xmm0", "xmm1"}
    args.update(incoming or {})
    memory = [{"addr": object_address(), "pointer": "object-vtable"},
              {"addr": object_address(8), "bytes": struct.pack("<I", count).hex()},
              {"addr": pointer("spare", 16), "pointer": "object-vtable"},
              {"addr": pointer("spare", 24), "bytes": struct.pack("<I", 17).hex()},
              {"addr": ALLOCATION_COUNT, "bytes": struct.pack("<I", allocation_count).hex()}]
    if alias:
        memory.append({"addr": object_address(0x0c), "bytes": fill * 4})
    for index, address in enumerate(OTHER_CLEANUPS + [CLEANUP]):
        memory.append({"addr": pointer("object-vtable", index * 8), "guest": address})
    for name, callback in [("a", ALLOCATOR_CALLBACK_A), ("b", ALLOCATOR_CALLBACK_B)]:
        memory.append({"addr": pointer("allocator-" + name, allocator_offset),
                       "pointer": "allocator-vtable-" + name})
        for index in range(5):
            memory.append({"addr": pointer("allocator-vtable-" + name, index * 8),
                           "guest": callback})
    if profile == "default-free":
        memory.append({"addr": ALLOCATOR_GLOBAL, "guest": DEFAULT_ALLOCATOR})
    else:
        memory.append({"addr": ALLOCATOR_GLOBAL, "pointer": f"allocator-a+{allocator_offset}"})

    assertion_writes = [] if assert_count is None else [
        {"addr": object_address(8), "bytes": struct.pack("<I", assert_count).hex()}]
    assertion = {"name": "_Assert", "argc": 2, "argi": [0, 1],
                 "ret": "0xfedcba9876543210", "writes": assertion_writes}
    cleanup_writes = []
    if cleanup_count is not None:
        cleanup_writes.append({"arg": 0, "offset": 8,
                               "bytes": struct.pack("<I", cleanup_count).hex()})
    if alias:
        # The count at +8 overwrites the default allocator object's vptr. Route
        # every reachable allocator call through owned A/B so it is never used.
        cleanup_writes.append({"arg": 0, "offset": 0x10,
                               "pointer": f"allocator-b+{allocator_offset}"})
    elif cleanup_instance:
        cleanup_writes.append({"arg": 0, "offset": 0x10, "pointer": "spare+64"})
    stubs = []
    for address in OTHER_CLEANUPS + [CLEANUP]:
        entry = {"address": address, "argc": 1, "argi": [0],
                 "argmem": [{"arg": 0, "size": PREFIX_BYTES}],
                 "ret": cleanup_result, "writes": cleanup_writes}
        stubs.extend(copy.deepcopy(entry) for _ in range(3))

    if profile == "default-free":
        free_writes = [] if free_count is None else [
            {"arg": 0, "offset": 8, "bytes": struct.pack("<I", free_count).hex()}]
        if free_allocation_count is not None:
            free_writes.append({"addr": ALLOCATION_COUNT,
                                "bytes": struct.pack("<I", free_allocation_count).hex()})
        free = {"name": "free", "argc": 1, "argi": [0],
                "argmem": [{"arg": 0, "size": PREFIX_BYTES}],
                "ret": free_result, "writes": free_writes}
        imports = [copy.deepcopy(assertion) for _ in range(3)]
        imports += [copy.deepcopy(free) for _ in range(3)]
    else:
        free_writes = [] if free_count is None else [
            {"arg": 1, "offset": 8, "bytes": struct.pack("<I", free_count).hex()}]
        targets = [FREE_WRAPPER] if profile == "protocol" else [
            ALLOCATOR_CALLBACK_A, ALLOCATOR_CALLBACK_B]
        for address in targets:
            free = {"address": address, "argc": 2, "argi": [0, 1],
                    "argmem": [{"arg": 0, "size": 8}, {"arg": 1, "size": PREFIX_BYTES}],
                    "ret": free_result, "writes": free_writes}
            stubs.extend(copy.deepcopy(free) for _ in range(3))
        imports = [copy.deepcopy(assertion) for _ in range(3)]

    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32", "args": args,
              "buffers": {"object": {"size": size, "bytes": fill * size},
                          "spare": {"size": OWNED_BYTES, "bytes": "d7" * OWNED_BYTES},
                          "object-vtable": {"size": 0x20},
                          "allocator-a": {"size": OWNED_BYTES, "bytes": "a6" * OWNED_BYTES},
                          "allocator-b": {"size": OWNED_BYTES, "bytes": "b7" * OWNED_BYTES},
                          "allocator-vtable-a": {"size": 0x28},
                          "allocator-vtable-b": {"size": 0x28}},
              "memory": memory, "imports": imports, "stubs": stubs}
    if stack:
        result["stack"] = list(stack)
    return result


def cases():
    """Distinguish widths, gates, callback ordering, rereads and untouched bytes."""
    result = []
    for profile in PROFILES:
        for value in [0, 1, 2, 0xff, 0x100, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-count-{value:08x}", value, profile))
        for value in [0, 1, 2, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-assert-count-{value:08x}", 0, profile,
                               assert_count=value))
        for offset in [0, 8, 24, 64]:
            for value in [0, 1, 2]:
                for fill in ["00", "ff"]:
                    result.append(case(f"{profile}-storage-{offset}-{value}-{fill}", value,
                                       profile, offset=offset, fill=fill))
        for value in [0, 1, 2]:
            result.append(case(f"{profile}-exact-prefix-{value}", value, profile,
                               offset=0, size=PREFIX_BYTES, fill="9d"))
        for value in [0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-cleanup-count-{value:08x}", 1, profile,
                               cleanup_count=value))
            result.append(case(f"{profile}-free-count-{value:08x}", 1, profile,
                               free_count=value))
        for value in ["0x1", "0x100000000", "0xffffffffffffffff"]:
            result.append(case(f"{profile}-ignored-results-{value[2:]}", 1, profile,
                               cleanup_result=value, free_result=value))
        result.append(case(profile + "-cleanup-instance", 1, profile, cleanup_instance=True))
    for profile in ["protocol", "allocator-dispatch"]:
        for name, count, after in [("direct", 1, None), ("assert-one", 0, 1),
                                   ("assert-two", 0, 2)]:
            result.append(case(f"{profile}-allocator-alias-{name}", count, profile,
                               alias=True, assert_count=after))
        for offset in [0, 8, 24, 64]:
            result.append(case(f"{profile}-allocator-offset-{offset}", 1, profile,
                               allocator_offset=offset))
    for profile in ["allocator-dispatch", "default-free"]:
        # The profile's ordinary count-one case already starts this counter at zero.
        for count in [1, 0x7fffffff, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-allocation-count-{count:08x}", 1, profile,
                               allocation_count=count))
    for count in [0, 1, 0x80000000, 0xffffffff]:
        result.append(case(f"default-free-callback-allocation-count-{count:08x}", 1,
                           "default-free", allocation_count=0x12345678,
                           free_allocation_count=count))
    # Every selected value differs from that register's ordinary entry state.
    ignored_values = {
        "rax": ["0x0", "0x1", "0x100000000", "0xffffffffffffffff"],
        "rsi": ["0x0", "0x1", "0x100000000", "0xffffffffffffffff"],
        "rdx": ["0x0", "0x1", "0x100000000"],
        "rcx": ["0x0", "0x1", "0xffffffffffffffff"],
        "r8": ["0x0", "0x1", "0x100000000", "0xffffffffffffffff"],
        "r9": ["0x0", "0x100000000", "0xffffffffffffffff"],
    }
    for register, values in ignored_values.items():
        for value in values:
            result.append(case(f"ignored-{register}-{value[2:]}", 1,
                               incoming={register: value}))
    for register in ["xmm0", "xmm1"]:
        for name, value in [("finite", "0000803f000000400000404000008040"),
                            ("nan", "0100807f" * 4)]:
            result.append(case(f"ignored-{register}-{name}", 1, incoming={register: value}))
    for name, values in [("zero", ["0x0"] * 4),
                         ("poison", ["0xffffffffffffffff", "0x100000000",
                                     "buf:spare+16", "0x8000000000000000"])]:
        result.append(case("ignored-stack-" + name, 1, stack=values))
    return result


def main():
    """Write deterministic, unique synthetic fixtures outside the source tree."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == 227 and len(generated) == len({row["id"] for row in generated})
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
