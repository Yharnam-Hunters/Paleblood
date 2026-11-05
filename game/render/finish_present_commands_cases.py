#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: finish_present_commands_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02aaf700"


def base_cases():
    for stage in ("empty", "mismatched_callback", "active", "needs_capacity"):
        c = basic(stage, returns="void", size=0xB520)
        c["buffers"]["self"]["bytes"] = "00" * 0xB520
        c["buffers"].update(
            {
                "payload": {"size": 128},
                "block": {"size": 32},
                "item": {"size": 64},
                "owner": {"size": 0xB740},
                "counters": {"size": 48},
                "producer": {"size": 8},
                "observed": {"size": 8},
            }
        )
        c["memory"] = [
            {"addr": "buf:self", "pointer": "payload"},
            {"addr": "buf:self+8", "pointer": "payload+64"},
            {"addr": "buf:self+16", "pointer": "payload+8"},
            {"addr": "buf:self+24", "guest": "0x014712b0"},
            {"addr": "0x057e43c8", "guest": "0x014712b0" if stage != "mismatched_callback" else "0x014784a0"},
            {"addr": "buf:self+46224", "pointer": "owner"},
            {"addr": "buf:owner+46888", "pointer": "counters"},
            {"addr": "buf:counters+24", "pointer": "producer"},
            {"addr": "buf:counters+32", "pointer": "observed"},
            {"addr": "buf:producer", "bytes": word(1)},
            {"addr": "buf:observed", "bytes": word(0 if stage != "needs_capacity" else 1)},
        ]
        if stage != "empty":
            c["memory"] += [
                {"addr": "buf:self+46296", "pointer": "block"},
                memory(46312, 1),
                memory(46316, 1),
                {"addr": "buf:block", "pointer": "item"},
            ]
        if stage in ("active", "needs_capacity"):
            c["stubs"] = [
                {"address": "0x02acd3f0", "argc": 2, "ret": 0},
                {"address": "0x02ad3c80", "argc": 3, "ret": 0},
            ]
            if stage == "needs_capacity":
                for preset in c["memory"]:
                    if preset["addr"] == "buf:self+8":
                        preset["pointer"] = "payload+8"
                        break
                c["stubs"].append({"address": "0x014712b0", "argc": 3, "ret": 0})
            else:
                c["stubs"].append({"address": "0x02ab0b00", "argc": 4, "ret": 0, "argmem": [{"arg": 2, "size": 64}]})
        yield c


def cases():
    yield from base_cases()
    for available in (6, 7, 8):
        c = next(c for c in base_cases() if c["id"] == "active")
        c["id"] = f"mismatched_counter_available_{available}"
        c["memory"].append({"addr": "buf:observed", "bytes": word(1)})
        c["memory"].append({"addr": "buf:self+8", "pointer": f"payload+{8 + available * 4}"})
        if available < 7:
            c["stubs"][-1] = {"address": "0x014712b0", "argc": 3, "ret": 0}
        else:
            c["stubs"][-1]["argmem"].append({"arg": 0, "size": 64})
        yield c
    c = next(c for c in base_cases() if c["id"] == "active")
    c["id"] = "multiple_buckets_single_list_and_block_owner"
    c["buffers"].update({name: {"size": 64} for name in ("node_a", "node_a2", "node_b", "single", "block_owner")})
    c["buffers"]["block_vtable"] = {"size": 128}
    c["memory"].extend(
        [
            {"addr": "buf:self+0xb3d8", "pointer": "node_a"},
            {"addr": "buf:self+0xb3e0", "pointer": "node_b"},
            {"addr": "buf:node_a+40", "pointer": "node_a2"},
            {"addr": "buf:self+0xb458", "pointer": "single"},
            {"addr": "buf:self+0xb460", "bytes": word(3)},
            {"addr": "buf:self+0xb4d0", "pointer": "block_owner"},
            {"addr": "buf:self+0xb4e0", "pointer": "block"},
            {"addr": "buf:block_owner", "pointer": "block_vtable"},
            {"addr": "buf:block_vtable+112", "guest": "0x01471280"},
        ]
    )
    c["stubs"] = (
        [{"address": "0x02aaa5a0", "argc": 3, "ret": 0} for _ in range(4)]
        + c["stubs"][:2]
        + [{"address": "0x01471280", "argc": 2, "ret": 0}]
        + c["stubs"][2:]
    )
    yield c

    c = next(c for c in base_cases() if c["id"] == "active")
    c["id"] = "multi_block_live_links_counts_and_cursor_publication"
    c["buffers"].update({"block_two": {"size": 32}})
    c["memory"] += [
        {"addr": "buf:self+0xb4e8", "bytes": word(2)},
        {"addr": "buf:block+8", "bytes": qword(0x2222)},
        {"addr": "buf:block+16", "pointer": "block_two"},
        {"addr": "buf:block_two", "bytes": qword(0x3333)},
    ]
    c["stubs"] = [
        {"address": "0x02acd3f0", "argc": 2, "ret": 0},
        {
            "address": "0x02ad3c80",
            "argc": 3,
            "ret": 0,
            "writes": [{"arg": 1, "offset": 16, "pointer": "payload+16", "size": 8}],
        },
        {
            "address": "0x02ad3c80",
            "argc": 3,
            "ret": 0,
            "argmem": [{"arg": 1, "offset": 0xB4B8, "size": 8}],
            "writes": [{"arg": 1, "offset": 16, "pointer": "payload+24", "size": 8}],
        },
        {"address": "0x02ad3c80", "argc": 3, "ret": 0, "argmem": [{"arg": 1, "offset": 0xB4B8, "size": 8}]},
        {"address": "0x02ab0b00", "argc": 4, "ret": 0, "argmem": [{"arg": 2, "size": 64}]},
    ]
    yield c
    c = next(c for c in base_cases() if c["id"] == "active")
    c["id"] = "item_callback_changes_link_and_last_count"
    c["buffers"]["block"]["size"] = 0xB520
    # Make the first block itself the command-state argument. The item callback
    # can then replace its link and count through the proven command argument.
    c["args"]["rdi"] = "buf:block"
    c["memory"] = [dict(m, addr=m["addr"].replace("buf:self", "buf:block")) for m in c["memory"]]
    c["memory"] += [
        {"addr": "buf:block+0xb4d8", "pointer": "block+0x100"},
        {"addr": "buf:block+0x100", "bytes": qword(0x1111)},
        {"addr": "buf:block+0x108", "pointer": "block+0x200"},
        {"addr": "buf:block+0x200", "bytes": qword(0x2222)},
        {"addr": "buf:block+0x208", "bytes": qword(0)},
        {"addr": "buf:block+0xb4ec", "bytes": word(0)},
    ]
    c["stubs"][1]["writes"] = [
        {"arg": 1, "offset": 0x108, "bytes": qword(0), "size": 8},
        {"arg": 1, "offset": 0xB4EC, "bytes": word(2), "size": 4},
    ]
    c["stubs"].insert(2, {"address": "0x02ad3c80", "argc": 3, "ret": 0})
    yield c

    c = next(c for c in base_cases() if c["id"] == "active")
    c["id"] = "release_callback_replaces_block_owner"
    c["buffers"].update(
        {
            "block_two": {"size": 32},
            "second_owner": {"size": 8},
            "first_vtable": {"size": 128},
            "second_vtable": {"size": 128},
        }
    )
    c["memory"] += [
        {"addr": "buf:block+8", "pointer": "block_two"},
        {"addr": "buf:block_two", "bytes": qword(0x2222)},
        {"addr": "buf:self+0xb4d0", "pointer": "self+0x100"},
        {"addr": "buf:self+0x100", "pointer": "first_vtable"},
        {"addr": "buf:second_owner", "pointer": "second_vtable"},
        {"addr": "buf:first_vtable+112", "guest": "0x01471280"},
        {"addr": "buf:second_vtable+112", "guest": "0x01471290"},
    ]
    c["stubs"] = (
        c["stubs"][:2]
        + [
            {"address": "0x02ad3c80", "argc": 3, "ret": 0},
            {
                "address": "0x01471280",
                "argc": 2,
                "ret": 0,
                "writes": [{"arg": 0, "offset": 0xB3D0, "pointer": "second_owner", "size": 8}],
            },
            {"address": "0x01471290", "argc": 2, "ret": 0},
        ]
        + c["stubs"][2:]
    )
    yield c
    c = next(c for c in base_cases() if c["id"] == "active")
    c["id"] = "drain_callback_replaces_saved_bucket_index"
    c["buffers"].update({"node": {"size": 64}, "other_node": {"size": 64}})
    c["memory"] += [
        {"addr": "buf:self+0xb3d8", "pointer": "node"},
        {"addr": "buf:self+0xb3e0", "pointer": "other_node"},
        {"addr": "buf:self+0xb460", "bytes": word(1)},
    ]
    c["stubs"].insert(
        0,
        {
            "address": "0x02aaa5a0",
            "argc": 3,
            "ret": 0,
            "writes": [{"arg": 1, "offset": -8, "bytes": qword(1), "size": 8}],
        },
    )
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
