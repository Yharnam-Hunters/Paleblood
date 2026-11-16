#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic IPfxContext::Instantiate cases. Usage: SCRIPT OUT_DIR.

Protocol cases script all three direct callees. Default-allocator cases execute
the original allocation dispatch and base initializer, while scripting malloc
and the GNM shim constructor. Returned storage is owned and aligned; NULL
allocation faults in the original and is outside successful comparison inputs.
"""
import copy
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb4ac0"
ALLOCATE = "0x00fb6f20"
BASE_INITIALIZE = "0x00fb6d20"
SHIM_INITIALIZE = "0x02a128b0"
ALLOCATOR_GLOBAL = "0x056b3618"
ALLOCATION_COUNT = "0x05866700"
DEFAULT_ALLOCATOR_VTABLE = "0x056b3630"
BASE_CONTEXT_VTABLE = "0x056b35e0"
CONTEXT_BYTES = 0x178
SHIM_OFFSET = 0x18
OWNED_BYTES = 0x240
PROFILES = ["protocol", "default-allocator"]


def pointer(name, offset=0):
    """Name owned storage without reproducing the harness address layout."""
    return f"buf:{name}+{offset}"


def base_initializer():
    """Model the three stores in the actual base initializer, preserving padding."""
    return {"address": BASE_INITIALIZE, "argc": 1, "argi": [0],
            "argmem": [{"arg": 0, "size": SHIM_OFFSET}],
            "ret": f"guest:{BASE_CONTEXT_VTABLE}",
            "writes": [{"arg": 0, "guest": BASE_CONTEXT_VTABLE},
                       {"arg": 0, "offset": 8, "bytes": struct.pack("<I", 1).hex()},
                       {"arg": 0, "offset": 0x10, "bytes": "00" * 8}]}


def case(cid, profile="protocol", allocation="allocation-a", offset=16,
         fill="a5", allocator_offset=16, count=0, callback_count=None,
         shim_effect="zero-range", shim_rax=None, incoming=None, stack=()):
    """Supply writable aligned objects and explicit, bounded helper observations."""
    assert profile in PROFILES and allocation in ["allocation-a", "allocation-b"]
    assert offset in [0, 16, 32, 64] and allocator_offset in [0, 8, 16, 32, 64]
    assert 0 <= count <= 0xffffffff and (callback_count is None or 0 <= callback_count <= 0xffffffff)
    assert offset + CONTEXT_BYTES <= OWNED_BYTES
    assert shim_effect in ["none", "zero-range", "tail"]
    other = "allocation-b" if allocation == "allocation-a" else "allocation-a"
    allocated = pointer(allocation, offset)
    # The real constructor's final memset leaves a non-object pointer in RAX.
    # Its language-level result is void; separate cases poison that ignored carrier.
    ignored_rax = shim_rax if shim_rax is not None else pointer(allocation, offset + 0x90)
    shim_writes = []
    if shim_effect == "zero-range":
        shim_writes = [{"arg": 0, "offset": 0x78, "bytes": "00" * 0xcc}]
    elif shim_effect == "tail":
        shim_writes = [{"arg": 0, "offset": 0x30, "bytes": "00"},
                       {"arg": 0, "offset": 0x158, "bytes": "00" * 8}]
    shim = {"address": SHIM_INITIALIZE, "argc": 1, "argi": [0],
            "argmem": [{"arg": 0, "offset": -SHIM_OFFSET, "size": CONTEXT_BYTES}],
            "ret": ignored_rax, "writes": shim_writes}
    args = {"rax": "0xfedcba9876543210", "rdi": "0x100000000", "rsi": "0x8000000000000000",
            "rdx": "0xffffffffffffffff", "rcx": "0x123456789abcdef", "r8": "0x1", "r9": "0x0"}
    assert not set(incoming or {}) - {"rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "xmm0", "xmm1"}
    args.update(incoming or {})
    stubs = []
    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i64", "args": args,
              "buffers": {"allocation-a": {"size": OWNED_BYTES, "bytes": (fill if allocation == "allocation-a" else "d8") * OWNED_BYTES},
                          "allocation-b": {"size": OWNED_BYTES, "bytes": (fill if allocation == "allocation-b" else "d8") * OWNED_BYTES},
                          "allocator": {"size": 0x100, "bytes": "b6" * 0x100}},
              "memory": [{"addr": ALLOCATOR_GLOBAL, "pointer": f"allocator+{allocator_offset}"},
                         {"addr": pointer("allocator", allocator_offset), "guest": DEFAULT_ALLOCATOR_VTABLE},
                         {"addr": ALLOCATION_COUNT, "bytes": struct.pack("<I", count).hex()}]}
    if profile == "protocol":
        for value in [allocated, pointer(other, offset)]:
            stubs.append({"address": ALLOCATE, "argc": 5, "argi": [0, 1, 2, 4],
                          "argu32": [3], "ret": value})
        stubs += [base_initializer(), base_initializer()]
    else:
        writes = [] if callback_count is None else [
            {"addr": ALLOCATION_COUNT, "bytes": struct.pack("<I", callback_count).hex()}]
        result["imports"] = [{"name": "malloc", "argc": 1, "argi": [0],
                              "ret": value, "writes": copy.deepcopy(writes)}
                             for value in [allocated, pointer(other, offset)]]
    # Queued second calls observe accidental repeats without unscripted errors.
    stubs += [shim, copy.deepcopy(shim)]
    result["stubs"] = stubs
    if stack:
        result["stack"] = list(stack)
    return result


def cases():
    """Cover protocol, actual dispatch, byte preservation and ignored entry state."""
    for profile in PROFILES:
        for allocation in ["allocation-a", "allocation-b"]:
            for offset in [0, 16, 32, 64]:
                for fill in ["00", "a5", "ff"]:
                    yield case(f"{profile}-{allocation}-{offset}-{fill}", profile, allocation, offset, fill)
    ignored_values = ["0x0", "0x1", "0x100000000", "0x8000000000000000",
              "0xffffffffffffffff", "buf:allocation-b+16"]
    for register in ["rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9"]:
        for index, value in enumerate(ignored_values):
            yield case(f"ignored-{register}-{index}", incoming={register: value})
    for register in ["xmm0", "xmm1"]:
        for index, value in enumerate(["00" * 16, "ff" * 16, "0100807f" * 4]):
            yield case(f"ignored-{register}-{index}", incoming={register: value})
    for index, stack_values in enumerate([["0x0"] * 4, ["0xffffffffffffffff"] * 4,
                                    ["buf:allocation-b+16", "0x100000000", "0x1", "0x8000000000000000"]]):
        yield case(f"ignored-stack-{index}", stack=stack_values)
    for profile in PROFILES:
        for offset in [0, 8, 16, 32, 64]:
            yield case(f"allocator-{profile}-{offset}", profile, allocator_offset=offset)
    for count in [0, 1, 0x7fffffff, 0x80000000, 0xffffffff]:
        for callback in [None, 0x7fffffff, 0xffffffff]:
            suffix = "none" if callback is None else f"{callback:08x}"
            yield case(f"count-{count:08x}-{suffix}", "default-allocator", count=count,
                       callback_count=callback)
    for index, value in enumerate(ignored_values[:5]):
        yield case(f"ignored-constructor-rax-{index}", shim_rax=value)
    for profile in PROFILES:
        for effect in ["none", "zero-range", "tail"]:
            yield case(f"effect-{profile}-{effect}", profile, shim_effect=effect)


def main():
    """Write deterministic synthetic cases with unique hyphen-only filenames."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len(generated) == 135 and len(generated) == len({row["id"] for row in generated})
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
