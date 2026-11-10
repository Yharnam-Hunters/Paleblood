#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: managed_present_capacity_cases.py OUT_DIR. Generate captures outside the repository.
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


def memory(offset, value):
    return {"addr": f"buf:self+{offset}", "bytes": word(value)}


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


ADDRESS = "0x02aaf030"


def base_cases():
    for allocation_failure in (False, True):
        for retry in (False, True):
            c = basic(f"allocation_failure_{int(allocation_failure)}_retry_{int(retry)}", returns="i8", size=0xB520)
            c["buffers"]["self"]["bytes"] = "00" * 0xB520
            c["buffers"].update(
                {
                    "payload": {"size": 128},
                    "acquired": {"size": 128},
                    "processor": {"size": 32},
                    "block": {"size": 64},
                    "owner": {"size": 16},
                    "vtable": {"size": 128},
                }
            )
            c["args"].update({"rsi": 0, "rdx": "buf:self"})
            c["memory"] = [
                {"addr": "buf:self", "pointer": "payload"},
                {"addr": "buf:self+8", "pointer": "payload+64"},
                {"addr": "buf:self+16", "pointer": "payload+8"},
                {"addr": "buf:self+46280", "pointer": "processor"},
                {"addr": "buf:processor+8", "bytes": qword(4)},
                memory(46312, 3),
                memory(46316, 0),
                {"addr": "buf:self+46288", "pointer": "owner"},
                {"addr": "buf:owner", "pointer": "vtable"},
                {"addr": "buf:vtable+88", "guest": "0x014712b0"},
            ]
            if not allocation_failure:
                c["memory"] += [
                    {"addr": "buf:self+46296", "pointer": "block"},
                    {"addr": "buf:self+46304", "pointer": "block"},
                ]
            c["stubs"] = [{"address": "0x02ad3d40", "argc": 2, "ret": "0x123456789abcdef0"}]
            if retry:
                c["stubs"] += [
                    {"address": "0x02ad3b80", "argc": 2, "ret": 1},
                    {"address": "0x0247f970", "argc": 0, "ret": 0},
                ]
            c["stubs"].append(
                {"address": "0x02ad3b80", "argc": 2, "ret": 0, "writes": [{"arg": 1, "pointer": "acquired", "size": 8}]}
            )
            if allocation_failure:
                c["stubs"].append({"address": "0x014712b0", "argc": 3, "ret": 0})
            else:
                c["stubs"].append({"address": "0x02ab0b00", "argc": 4, "ret": 0, "argmem": [{"arg": 2, "size": 64}]})
            yield c


def cases():
    """Exercise cached pool sizes, block links, and callback-mutated management state."""
    yield from base_cases()
    for stage in ("new_head", "append_tail", "zero_slot"):
        c = next(c for c in base_cases() if c["id"] == "allocation_failure_1_retry_0")
        c["id"] = f"distinct_state_{stage}"
        c["buffers"]["state"] = {"size": 0xB520}
        c["buffers"]["old_block"] = {"size": 64}
        c["buffers"]["block"]["bytes"] = "a5" * 64
        # Preserve distinct live command storage and management state.
        for memory_entry in c["memory"]:
            if memory_entry["addr"].startswith("buf:self+") and int(memory_entry["addr"].split("+")[1], 0) >= 0xB3D0:
                memory_entry["addr"] = memory_entry["addr"].replace("buf:self+", "buf:state+")
        c["args"]["rdx"] = "buf:state"
        if stage == "append_tail":
            c["memory"].extend(
                [
                    {"addr": "buf:state+0xb4d8", "pointer": "old_block"},
                    {"addr": "buf:state+0xb4e0", "pointer": "old_block"},
                    {"addr": "buf:state+0xb4ec", "bytes": word(3)},
                ]
            )
        if stage == "zero_slot":
            c["memory"].extend(
                [
                    {"addr": "buf:state+0xb4d8", "pointer": "old_block"},
                    {"addr": "buf:state+0xb4e0", "bytes": qword(0)},
                    {"addr": "buf:state+0xb4ec", "bytes": word(0)},
                ]
            )
            c["stubs"] = c["stubs"][:-1]
        else:
            c["stubs"][-1]["ret"] = "buf:block"
            c["stubs"].append({"address": "0x02ab0b00", "argc": 4, "ret": 0, "argmem": [{"arg": 2, "size": 64}]})
        yield c

    c = next(c for c in base_cases() if c["id"] == "allocation_failure_0_retry_0")
    c["id"] = "existing_head_and_distinct_tail"
    c["buffers"]["head"] = {"size": 64, "bytes": "6a" * 64}
    c["memory"].extend(
        [{"addr": "buf:self+0xb4d8", "pointer": "head"}, {"addr": "buf:self+0xb4e0", "pointer": "block"}]
    )
    yield c
    c = next(c for c in base_cases() if c["id"] == "allocation_failure_0_retry_0")
    c["id"] = "count_above_link_index_does_not_allocate"
    c["memory"].append({"addr": "buf:self+0xb4ec", "bytes": word(4)})
    # An observing allocator is installed even though this path must not call it.
    c["stubs"].insert(-1, {"address": "0x014712b0", "argc": 3, "ret": 0})
    yield c

    c = next(c for c in base_cases() if c["id"] == "allocation_failure_1_retry_0")
    c["id"] = "allocate_appends_after_distinct_head_and_tail"
    c["buffers"].update({"head": {"size": 64, "bytes": "6a" * 64}, "tail": {"size": 64, "bytes": "7b" * 64}})
    c["memory"].extend(
        [
            {"addr": "buf:self+0xb4d8", "pointer": "head"},
            {"addr": "buf:self+0xb4e0", "pointer": "tail"},
            {"addr": "buf:self+0xb4ec", "bytes": word(3)},
            {"addr": "buf:head+24", "pointer": "tail"},
        ]
    )
    c["stubs"][-1]["ret"] = "buf:block"
    c["stubs"].append({"address": "0x02ab0b00", "argc": 4, "ret": 0, "argmem": [{"arg": 2, "size": 64}]})
    yield c


    case = next(case for case in base_cases() if case["id"] == "allocation_failure_0_retry_1")
    case["id"] = "slot_count_captured_before_pop_changes_it"
    first_pop = next(stub for stub in case["stubs"] if stub["address"] == "0x02ad3b80")
    first_pop["writes"] = [{"arg": 0, "offset": 8, "bytes": qword(8), "size": 8}]
    yield case

    case = next(case for case in base_cases() if case["id"] == "allocation_failure_1_retry_0")
    case["id"] = "link_index_above_one_byte"
    case["buffers"]["block"] = {"size": 4096, "bytes": "a5" * 4096}
    case["memory"].extend([
        {"addr": "buf:self+0xb4d8", "bytes": qword(0)},
        {"addr": "buf:self+0xb4e0", "bytes": qword(0)},
        {"addr": "buf:self+0xb4e8", "bytes": word(300)},
    ])
    case["stubs"][-1]["ret"] = "buf:block"
    case["stubs"].append({"address": "0x02ab0b00", "argc": 4, "ret": 0,
                          "argmem": [{"arg": 2, "size": 64}]})
    yield case

    case = next(case for case in base_cases() if case["id"] == "allocation_failure_0_retry_0")
    case["id"] = "snapshot_delegate_changes_live_cursor"
    case["stubs"][-1]["writes"] = [{"arg": 1, "offset": 16, "pointer": "acquired+16", "size": 8}]
    yield case


def main():
    """Write deterministic synthetic cases to the requested directory."""
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
