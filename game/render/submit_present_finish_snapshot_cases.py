#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: submit_present_finish_snapshot_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ab0b00"


def base_cases():
    for first_size, second_size in ((0, 0), (1, 3), (4, 0), (0, 8), (12, 24)):
        c = basic(f"sizes_{first_size}_{second_size}", returns="void", size=0xB520)
        c["buffers"]["self"]["bytes"] = "00" * 0xB520
        c["buffers"].update(
            {
                "commands": {"size": 64},
                "snapshot": {"size": 64},
                "payload": {"size": 128},
                "owner": {"size": 0xB740},
                "counters": {"size": 48},
                "producer": {"size": 8},
                "observed": {"size": 8},
            }
        )
        c["args"].update({"rsi": "buf:commands", "rdx": "buf:snapshot", "rcx": "0xffffffff"})
        c["memory"] = [
            {"addr": "buf:self+46224", "pointer": "owner"},
            {"addr": "buf:owner+46888", "pointer": "counters"},
            {"addr": "buf:counters+24", "pointer": "producer"},
            {"addr": "buf:counters+32", "pointer": "observed"},
            {"addr": "buf:producer", "bytes": word(1)},
            {"addr": "buf:commands", "pointer": "payload"},
            {"addr": "buf:commands+16", "pointer": f"payload+{first_size}"},
            {"addr": "buf:snapshot", "pointer": "payload+64"},
            {"addr": "buf:snapshot+16", "pointer": f"payload+{64+second_size}"},
        ]
        n = int(bool(first_size & ~3)) + int(bool(second_size & ~3))
        c["stubs"] = [
            {
                "address": "0x01489a10",
                "argc": 5,
                "ret": 0,
                "argmem": [{"arg": 1, "size": max(n, 1) * 8}, {"arg": 2, "size": max(n, 1) * 4}],
            }
        ]
        yield c


def cases():
    yield from base_cases()
    for producer, observed in ((2, 0), (2, 0x80000000), (0, 0), (0x90000000, 0x80000000)):
        c = next(c for c in base_cases() if c["id"] == "sizes_12_24")
        c["id"] = f"counter_producer_{producer:08x}_observed_{observed:08x}"
        c["memory"].extend(
            [
                {"addr": "buf:producer", "bytes": word(producer)},
                {"addr": "buf:observed", "bytes": word(observed)},
                {"addr": "buf:counters+16", "bytes": qword(0x123456789ABCDEF0)},
            ]
        )
        target = (producer - 1) & 0xFFFFFFFF
        operations = []
        if observed & 0x80000000 and not target & 0x80000000:
            operations.append({"address": "0x01477b30", "argc": 5, "ret": 0})
        operations.append({"address": "0x01477b30", "argc": 5, "ret": 0})
        c["stubs"] = operations + c["stubs"]
        yield c
    c = next(c for c in base_cases() if c["id"] == "sizes_12_24")
    c["id"] = "counter_live_label_and_output_pointer_reload"
    c["buffers"]["commands"]["size"] = 128
    c["buffers"]["new_observed"] = {"size": 8}
    c["memory"].extend(
        [
            {"addr": "buf:owner+46888", "pointer": "commands+64"},
            {"addr": "buf:commands+80", "bytes": qword(0x1122334455667788)},
            {"addr": "buf:commands+88", "pointer": "producer"},
            {"addr": "buf:commands+96", "pointer": "observed"},
            {"addr": "buf:producer", "bytes": word(2)},
            {"addr": "buf:observed", "bytes": word(0x80000000)},
        ]
    )
    c["stubs"] = [
        {
            "address": "0x01477b30",
            "argc": 5,
            "ret": 0,
            "writes": [{"arg": 0, "offset": 80, "bytes": qword(0x8877665544332211), "size": 8}],
        },
        {
            "address": "0x01477b30",
            "argc": 5,
            "ret": 0,
            "writes": [{"arg": 0, "offset": 96, "pointer": "new_observed", "size": 8}],
        },
    ] + c["stubs"]
    yield c

    for size in (0, 24):
        c = next(c for c in base_cases() if c["id"] == "sizes_0_8")
        c["id"] = f"no_optional_snapshot_size_{size}"
        c["args"]["rsi"] = 0
        c["memory"].append({"addr": "buf:snapshot+16", "pointer": f"payload+{64 + size}"})
        c["stubs"] = [
            {"address": "0x01471270", "argc": 5, "ret": 0, "writes": [{"arg": 0, "bytes": "00" * 24, "size": 24}]},
            {"address": "0x01489a10", "argc": 5, "ret": 0, "argmem": [{"arg": 1, "size": 8}, {"arg": 2, "size": 4}]},
        ]
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
