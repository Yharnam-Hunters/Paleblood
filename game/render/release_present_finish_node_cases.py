#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: release_present_finish_node_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02aaa5a0"


def cases():
    for retries in (0, 1, 2):
        for result in (0, 0x89ABCDEF12345678):
            c = basic(f"retries_{retries}_result_{result:x}", size=256)
            c["buffers"].update(
                {"commands": {"size": 64}, "command_slot": {"size": 8}, "node": {"size": 64, "bytes": "a5" * 64}}
            )
            c["args"].update({"rsi": "buf:command_slot", "rdx": "buf:node"})
            c["memory"] = [
                {"addr": "buf:self+152", "bytes": qword(0x1234)},
                {"addr": "buf:command_slot", "pointer": "commands"},
            ]
            c["stubs"] = [
                {"address": "0x0247ebc0", "argc": 3, "ret": hex(0x1235 if i < retries else 0x1234)}
                for i in range(retries + 1)
            ]
            c["stubs"].append({"address": "0x014778f0", "argc": 8, "ret": hex(result)})
            yield c

    c = basic("failed_exchange_reloads_changed_head", size=256)
    c["buffers"].update({"commands": {"size": 64}, "command_slot": {"size": 8}, "node": {"size": 64}})
    c["args"].update({"rsi": "buf:command_slot", "rdx": "buf:node"})
    c["memory"] = [
        {"addr": "buf:self+152", "bytes": qword(0x1234)},
        {"addr": "buf:command_slot", "pointer": "commands"},
    ]
    c["stubs"] = [
        {
            "address": "0x0247ebc0",
            "argc": 3,
            "ret": "0x1235",
            "writes": [{"arg": 0, "bytes": qword(0x5678), "size": 8}],
        },
        {"address": "0x0247ebc0", "argc": 3, "ret": "0x5678"},
        {"address": "0x014778f0", "argc": 8, "ret": 0},
    ]
    yield c

    case = basic("command_slot_aliases_exchanged_head", size=256)
    case["buffers"].update({"commands": {"size": 64}, "node": {"size": 64}})
    case["args"].update({"rsi": "buf:self+152", "rdx": "buf:node"})
    case["memory"] = [{"addr": "buf:self+152", "pointer": "commands"}]
    case["stubs"] = [
        {"address": "0x0247ebc0", "argc": 3, "ret": "buf:commands",
         "writes": [{"arg": 0, "pointer": "node", "size": 8}]},
        {"address": "0x014778f0", "argc": 8, "ret": 0},
    ]
    yield case


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    # Capture defined low bits of integer parameters, excluding ABI padding.
    for entry in generated:
        for callback in entry.get("stubs", []):
            address = callback["address"]
            if address == "0x014778f0":
                callback.update(argi=[0, 3, 5], argu32=[1, 2, 4, 6, 7])
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
