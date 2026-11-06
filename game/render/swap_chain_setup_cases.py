#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: swap_chain_setup_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys

MASK64 = (1 << 64) - 1


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def basic(cid, returns="i64", size=512):
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": returns,
        "args": {"rdi": "buf:self"},
        "buffers": {"self": {"size": size, "bytes": "a5" * size}},
        "memory": [],
        "stubs": [],
    }


ADDRESS = "0x02ad5e90"


def initial_rejection_cases():
    for stage in ("selector_rejected", "allocation_failure", "selector_failure", "unit_failure"):
        c = basic(stage, size=0x400)
        c["buffers"]["self"]["bytes"] = "00" * 0x400
        c["buffers"].update(
            {
                "output": {"size": 16, "bytes": "a5" * 16},
                "resource": {"size": 16},
                "services": {"size": 128},
                "info": {"size": 80},
                "manager": {"size": 16},
                "object": {"size": 128},
            }
        )
        c["args"].update(
            {"rsi": "buf:output", "rdx": "buf:resource", "rcx": "buf:services", "r8": "buf:info", "r9": "buf:manager"}
        )
        c["stack"] = ["buf:manager"]
        c["memory"] = [{"addr": "buf:info+16", "bytes": word(0 if stage == "selector_rejected" else 0x57)}]
        if stage != "selector_rejected":
            c["stubs"] = [
                {"address": "0x0247c4e0", "argc": 6, "ret": 0 if stage == "allocation_failure" else "buf:object"}
            ]
            if stage != "allocation_failure":
                c["stubs"] += [
                    {"address": "0x02adbcb0", "argc": 2, "ret": 0},
                    {
                        "address": "0x02ab7310",
                        "argc": 2,
                        "ret": 12 if stage == "selector_failure" else 0,
                        "writes": [{"arg": 0, "bytes": word(1), "size": 4}],
                    },
                ]
                if stage == "unit_failure":
                    c["stubs"] += [
                        {"address": "0x0149cb30", "argc": 4, "ret": 4},
                        {"address": "0x02abb4a0", "argc": 1, "ret": 0},
                    ]
        yield c


def cases():
    yield from initial_rejection_cases()
    stages = (
        "record_failure",
        "storage_failure",
        "storage_rejected",
        "video_open_failure",
        "flip_rate_failure",
        "queue_failure",
        "event_failure",
        "register_failure",
        "cached_failure",
        "upload_failure",
        "curve_failure",
        "success",
        "fallback_success",
    )
    for count in (0, 2):
        for stage in stages:
            yield setup_route(stage, count, 8 if count else 1)

    yield from additional_cases()


def setup_route(stage, count, unit):
    c = basic(f"{stage}_count{count}_unit{unit}", size=0x3500)
    c["buffers"]["self"]["bytes"] = "00" * 0x3500
    c["buffers"].update(
        {
            "output": {"size": 16, "bytes": "a5" * 16},
            "resource": {"size": 16},
            "services": {"size": 128},
            "info": {"size": 80},
            "manager": {"size": 16},
            "vtable": {"size": 128},
            "object": {"size": 128},
            "storage": {"size": 256, "bytes": "6b" * 256},
            "upload": {"size": 0x3010, "bytes": "6c" * 0x3010},
        }
    )
    c["args"].update(
        {"rsi": "buf:output", "rdx": "buf:resource", "rcx": "buf:services", "r8": "buf:info", "r9": "buf:manager"}
    )
    c["stack"] = ["buf:manager"]
    c["memory"] = [
        {"addr": "buf:manager", "pointer": "vtable"},
        {"addr": "buf:vtable+80", "guest": "0x01471280"},
        {"addr": "buf:vtable+88", "guest": "0x01471270"},
        {"addr": "buf:vtable+112", "guest": "0x01471290"},
        {"addr": "buf:info", "bytes": word(0x1234) + word(0x5678)},
        {"addr": "buf:info+16", "bytes": word(0x57)},
        {"addr": "buf:info+28", "bytes": word(0x12345678) + word(0x89ABCDEF) + word(0x7)},
        {"addr": "buf:info+40", "bytes": word(count)},
        {"addr": "buf:info+60", "bytes": word(0 if count else 0x88) + word(0x4321)},
    ]

    def s(address, argc, ret=0, **extra):
        item = {"address": address, "argc": argc, "ret": ret, **extra}
        c["stubs"].append(item)
        return item

    def cleanup(queue=False, handle=True):
        s("0x02571c60", 1)
        s("0x02572290", 1)
        if queue:
            s("0x02fbf5b8", 1)
        if handle:
            s("0x02fbf5c8", 1)
        s("0x01471290", 2)
        s("0x02adbcc0", 1)
        s("0x01471290", 2)

    s("0x0247c4e0", 6, "buf:object", argmem=[{"arg": 4, "size": 72}])
    s("0x02adbcb0", 2)
    s("0x02ab7310", 2, 0, writes=[{"arg": 0, "bytes": word(3), "size": 4}])
    s("0x0149cb30", 4, 0, writes=[{"arg": 0, "bytes": word(unit), "size": 4}])
    s("0x02ab7a20", 2, 0x123)
    s("0x02ab7ab0", 3, 0x456)
    s(
        "0x01485f90",
        10,
        0 if stage == "record_failure" else "0x0000000800000010",
        argi=[0, 8, 9],
        argu32=[1, 2, 3, 4, 5, 6, 7],
        writes=[{"arg": 0, "bytes": ("01020304" * 11), "size": 44}],
    )
    if stage == "record_failure":
        return c
    s("0x01471270", 4, 0 if stage == "storage_failure" else "buf:storage")
    if stage == "storage_failure":
        s("0x02adbcc0", 1)
        s("0x01471290", 2)
        return c
    s("0x02abadd0", 1, 0 if stage == "storage_rejected" else 1)
    if stage == "storage_rejected":
        s("0x01471290", 2)
        s("0x02adbcc0", 1)
        s("0x01471290", 2)
        return c
    s("0x02fbe688", 3, "buf:storage")
    for index in range(count):
        s("0x02abaeb0", 1, 0x123 + index)
        s(
            "0x01486c50",
            2,
            0,
            argmem=[{"arg": 0, "size": 44}],
            writes=[{"arg": 0, "bytes": qword(0x123 + index), "size": 8}],
        )
    s("0x025676b0", 1, "0x123456789abcdef0")
    s("0x02fbf568", 4, "0x80290001" if stage == "video_open_failure" else 7)
    if stage == "video_open_failure":
        cleanup(handle=False)
        return c
    s("0x02fbf578", 2, "0x80000001" if stage == "flip_rate_failure" else 0)
    if stage == "flip_rate_failure":
        cleanup()
        return c
    s(
        "0x02fbf588",
        2,
        "0x80000001" if stage == "queue_failure" else 0,
        writes=[] if stage == "queue_failure" else [{"arg": 0, "bytes": qword(0x77), "size": 8}],
    )
    if stage == "queue_failure":
        cleanup()
        return c
    s("0x02fbf598", 3, "0x80000001" if stage == "event_failure" else 0)
    if stage == "event_failure":
        cleanup(queue=True)
        return c
    s("0x02fbf5a8", 7, 0, argi=[0], argu32=[1, 2, 3, 4, 5, 6], writes=[{"arg": 0, "bytes": "5a" * 44, "size": 44}])
    for index in range(count):
        s("0x01486ca0", 1, 0x123 + index, argmem=[{"arg": 0, "size": 44}])
    s(
        "0x02fbf5d8",
        5,
        "0x80000001" if stage == "register_failure" else 0,
        argmem=[{"arg": 2, "size": count * 8}, {"arg": 4, "size": 44}],
    )
    if stage == "register_failure":
        cleanup(queue=True)
        return c
    s("0x02565f90", 7, 0x12 if stage == "cached_failure" else 0, argmem=[{"arg": 2, "size": 48}])
    if stage == "cached_failure":
        return c
    s("0x02abaef0", 0, 0x123)
    s("0x0247d5b0", 0, "0x123456789abcdef0")
    s("0x0247d4b0", 2, 0 if stage in ("upload_failure", "fallback_success") else "buf:upload")
    if stage in ("upload_failure", "fallback_success"):
        s("0x01471280", 2, 0 if stage == "upload_failure" else "buf:upload")
    if stage != "upload_failure":
        s("0x02567890", 1, "0x12345678")
        s("0x02571790", 7, 0x13 if stage == "curve_failure" else 0, argmem=[{"arg": 3, "size": 48}])
        if stage == "fallback_success":
            s("0x01471290", 2)
    s("0x0247d5f0", 1)
    return c


def additional_cases():
    for stage in (
        "storage_failure",
        "storage_rejected",
        "video_open_failure",
        "register_failure",
        "cached_failure",
        "curve_failure",
        "success",
    ):
        c = setup_route(stage, 2, 8)
        c["id"] = f"distinct_allocator_{stage}"
        c["buffers"]["allocator"] = {"size": 16}
        c["stack"] = ["buf:allocator"]
        c["memory"].append({"addr": "buf:allocator", "pointer": "vtable"})
        yield c
    c = setup_route("register_failure", 2, 8)
    c["id"] = "distinct_allocator_cleanup_reloads_output_after_storage_release"
    c["buffers"]["allocator"] = {"size": 32}
    c["buffers"]["replacement_object"] = {"size": 32}
    # The output slot is a field in the allocator allocation, so its release
    # callback can replace that slot. Cleanup must destroy the new object.
    c["args"]["rsi"] = "buf:allocator+8"
    c["stack"] = ["buf:allocator"]
    c["memory"].append({"addr": "buf:allocator", "pointer": "vtable"})
    first_release = next(s for s in c["stubs"] if s["address"] == "0x01471290")
    first_release["writes"] = [{"arg": 0, "offset": 8, "pointer": "replacement_object", "size": 8}]
    yield c

    c = setup_route("success", 2, 8)
    c["id"] = "unaligned_record_size_19"
    next(stub for stub in c["stubs"] if stub["address"] == "0x01485f90")["ret"] = "0x0000000800000013"
    yield c
    for flags in (0, 1, 2, 4):
        c = setup_route("success", 2, 8)
        c["id"] = f"independent_setup_flag_{flags}"
        c["memory"].append({"addr": "buf:info+36", "bytes": word(flags)})
        yield c


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
