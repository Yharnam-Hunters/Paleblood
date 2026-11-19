#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic interface cleanup/free cases. Usage: SCRIPT OUT_DIR.

Protocol cases script both calls. Allocator-dispatch cases execute the original
free wrapper, and default-free cases also execute its original default method.
No storage is actually freed. The allocator-reload aliases use writable image
storage; original readonly tables are never modified.
"""
import copy
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX


ADDRESS = "0x00fb6ce0"
FREE_WRAPPER = "0x00fb6f50"
CLEANUP = "0x00fb6da0"
OTHER_CLEANUPS = ["0x00fb18b0", "0x00fb48d0", "0x00fb6fe0"]
# These are scripted call identities, not recovered original prototypes. Neither
# address is the selected function or one of its executing original callees.
ALLOCATOR_CALLBACK_A = "0x00fb6f20"
ALLOCATOR_CALLBACK_B = "0x00fb6d40"
ALLOCATOR_GLOBAL = "0x056b3618"
ALLOCATION_COUNT = "0x05866700"
DEFAULT_ALLOCATOR = "0x056b3610"
OBJECT_ALIAS = "0x056b3608"
PREFIX_BYTES = 0x18
OWNED_BYTES = 0x80
PROFILES = ["protocol", "allocator-dispatch", "default-free"]


def pointer(name, offset=0):
    """Use the harness's owned-buffer references rather than copied addresses."""
    return f"buf:{name}+{offset}"


def case(cid, count=2, profile="protocol", offset=16, fill="a5", size=OWNED_BYTES,
         null=False, cleanup_count=None, free_count=None, alias=False,
         allocator_offset=16, allocation_count=0, free_allocation_count=None,
         cleanup_instance=False, cleanup_result="0x0", free_result="0x0",
         first_object=False):
    """Create valid prefixes and bounded, independently observing callbacks."""
    assert profile in PROFILES and offset >= 0 and offset % 8 == 0
    assert offset + PREFIX_BYTES <= size and allocator_offset % 8 == 0
    assert 0 <= allocator_offset <= OWNED_BYTES - 8
    assert all(value is None or 0 <= value <= 0xffffffff for value in
               [count, cleanup_count, free_count, allocation_count, free_allocation_count])
    assert not alias or (profile != "default-free" and not cleanup_instance and not null)
    assert free_allocation_count is None or profile == "default-free"
    assert not null or (cleanup_count is None and free_count is None and not cleanup_instance)
    assert not first_object or (offset == 0 and not alias and not null)
    object_name = "a-object" if first_object else "object"

    def object_address(field=0):
        if alias:
            return f"0x{int(OBJECT_ALIAS, 16) + field:08x}"
        return pointer(object_name, offset + field)

    self_pointer = "0x0" if null else (
        f"guest:{OBJECT_ALIAS}" if alias else pointer(object_name, offset))
    args = {"rdi": self_pointer, "rsi": pointer("spare", 16),
            "rax": "0xfedcba9876543210", "rdx": "0xffffffffffffffff",
            "rcx": "0x100000000", "r8": "0x8000000000000000", "r9": "0x1"}
    memory = [{"addr": object_address(), "pointer": "object-vtable"},
              {"addr": object_address(8), "bytes": struct.pack("<I", count).hex()},
              {"addr": pointer("spare", 16), "pointer": "object-vtable"},
              {"addr": pointer("spare", 24), "bytes": struct.pack("<I", 17).hex()},
              {"addr": ALLOCATION_COUNT, "bytes": struct.pack("<I", allocation_count).hex()}]
    if alias:
        memory.append({"addr": object_address(0x0c), "bytes": fill * 4})
    else:
        memory.append({"addr": object_address(0x10), "pointer": "spare+16"})
    for index, address in enumerate(OTHER_CLEANUPS + [CLEANUP]):
        memory.append({"addr": pointer("object-vtable", index * 8), "guest": address})
    for name, callback in [("a", ALLOCATOR_CALLBACK_A), ("b", ALLOCATOR_CALLBACK_B)]:
        memory.append({"addr": pointer("allocator-" + name, allocator_offset),
                       "pointer": "allocator-vtable-" + name})
        for index in range(5):
            memory.append({"addr": pointer("allocator-vtable-" + name, index * 8),
                           "guest": callback})
    memory.append({"addr": ALLOCATOR_GLOBAL, "guest": DEFAULT_ALLOCATOR} if profile == "default-free"
                  else {"addr": ALLOCATOR_GLOBAL, "pointer": f"allocator-a+{allocator_offset}"})

    cleanup_writes = [] if cleanup_count is None else [
        {"arg": 0, "offset": 8, "bytes": struct.pack("<I", cleanup_count).hex()}]
    if alias:
        # The instance field aliases the allocator global. The count overwrites
        # the default allocator's vptr, so this profile always uses owned A/B.
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
    # An unexpected assertion is observable for wrong count-release controls.
    assertion = {"name": "_Assert", "argc": 2, "argi": [0, 1], "ret": "0x0"}
    imports = [copy.deepcopy(assertion) for _ in range(3)]
    if profile == "default-free":
        writes = [] if free_count is None else [
            {"arg": 0, "offset": 8, "bytes": struct.pack("<I", free_count).hex()}]
        if free_allocation_count is not None:
            writes.append({"addr": ALLOCATION_COUNT,
                           "bytes": struct.pack("<I", free_allocation_count).hex()})
        free = {"name": "free", "argc": 1, "argi": [0], "ret": free_result, "writes": writes}
        if not null:
            free["argmem"] = [{"arg": 0, "size": PREFIX_BYTES}]
        imports.extend(copy.deepcopy(free) for _ in range(3))
    else:
        writes = [] if free_count is None else [
            {"arg": 1, "offset": 8, "bytes": struct.pack("<I", free_count).hex()}]
        targets = [FREE_WRAPPER] if profile == "protocol" else [
            ALLOCATOR_CALLBACK_A, ALLOCATOR_CALLBACK_B]
        for address in targets:
            reads = [{"arg": 0, "size": 8}]
            if not null:
                reads.append({"arg": 1, "size": PREFIX_BYTES})
            free = {"address": address, "argc": 2, "argi": [0, 1], "argmem": reads,
                    "ret": free_result, "writes": writes}
            stubs.extend(copy.deepcopy(free) for _ in range(3))
    buffers = {object_name: {"size": size, "bytes": fill * size},
               "spare": {"size": OWNED_BYTES, "bytes": "d7" * OWNED_BYTES},
               "object-vtable": {"size": 0x20},
               "allocator-a": {"size": OWNED_BYTES, "bytes": "a6" * OWNED_BYTES},
               "allocator-b": {"size": OWNED_BYTES, "bytes": "b7" * OWNED_BYTES},
               "allocator-vtable-a": {"size": 0x28}, "allocator-vtable-b": {"size": 0x28}}
    if first_object:
        assert min(buffers) == object_name and SANDBOX & 0xffffffff == 0
    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void", "args": args,
              "buffers": buffers, "memory": memory, "imports": imports, "stubs": stubs}
    assert all(stub["address"] != ADDRESS for stub in stubs)
    return result


def cases():
    """Distinguish the null gate, call order, allocator reload and preserved bytes."""
    result = [case("null-input", null=True),
              case("nonnull-high-only-pointer", offset=0, first_object=True)]
    # Seven payload pages and their guard move the object from +0x8000 to
    # +0x10000. Its low sixteen bits are zero while its low DWORD is nonzero.
    low16 = case("nonnull-low16-zero-pointer", offset=0)
    low16["buffers"]["a-padding"] = {"size": 7 * PAGE}
    assert (SANDBOX + 16 * PAGE) & 0xffff == 0
    assert (SANDBOX + 16 * PAGE) & 0xffffffff != 0
    result.append(low16)
    for profile in PROFILES:
        result.append(case(profile + "-basic", profile=profile))
        for value in [0, 1, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-count-{value:08x}", value, profile))
            result.append(case(f"{profile}-cleanup-count-{value:08x}", profile=profile,
                               cleanup_count=value))
        for offset in [0, 8, 24, 64]:
            for fill in ["00", "ff"]:
                result.append(case(f"{profile}-storage-{offset}-{fill}", profile=profile,
                                   offset=offset, fill=fill))
        result.append(case(profile + "-exact-prefix", profile=profile, offset=0,
                           size=PREFIX_BYTES, fill="9d"))
        for value in [0, 0xffffffff]:
            result.append(case(f"{profile}-free-count-{value:08x}", profile=profile, free_count=value))
        result.append(case(profile + "-cleanup-instance", profile=profile, cleanup_instance=True))
        for value in ["0x1", "0x100000000", "0xffffffffffffffff"]:
            result.append(case(f"{profile}-ignored-results-{value[2:]}", profile=profile,
                               cleanup_result=value, free_result=value))
    for profile in ["protocol", "allocator-dispatch"]:
        result.append(case(profile + "-allocator-alias", profile=profile, alias=True))
        for offset in [0, 8, 64]:
            result.append(case(f"{profile}-allocator-offset-{offset}", profile=profile,
                               allocator_offset=offset))
    for profile in ["allocator-dispatch", "default-free"]:
        for value in [1, 0x80000000, 0xffffffff]:
            result.append(case(f"{profile}-allocation-count-{value:08x}", profile=profile,
                               allocation_count=value))
    for value in [0, 1, 0xffffffff]:
        result.append(case(f"default-free-callback-count-{value:08x}", profile="default-free",
                           allocation_count=0x12345678, free_allocation_count=value))
    return result


def main():
    """Write deterministic, unique fixtures outside the public source tree."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len(generated) == 92 and len({row["id"] for row in generated}) == 92
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
