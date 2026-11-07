#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: command_submit_present_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF).hex()


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


ADDRESS = "0x02aafb30"


def base_cases():
    for available in (0, 64, 256):
        for same in (False, True):
            c = basic(f"capacity_{available}_same_counter_{int(same)}", returns="i32", size=0xB520)
            c["buffers"]["self"]["bytes"] = "00" * 0xB520
            c["buffers"].update(
                {
                    "payload": {"size": 2048},
                    "owner": {"size": 0xB740},
                    "counters": {"size": 48},
                    "producer": {"size": 8},
                    "observed": {"size": 8},
                }
            )
            c["args"].update({"rsi": "0x80000001", "rdx": "0x89abcdef", "rcx": 3, "r8": "0x123456789abcdef0"})
            c["memory"] = [
                {"addr": "buf:self", "pointer": "payload"},
                {"addr": "buf:self+8", "pointer": f"payload+{available*4}"},
                {"addr": "buf:self+16", "pointer": "payload"},
                {"addr": "buf:self+24", "guest": "0x014712b0"},
                {"addr": "buf:self+32", "bytes": qword(7)},
                {"addr": "buf:self+46224", "pointer": "owner"},
                {"addr": "buf:owner+46888", "pointer": "counters"},
                {"addr": "buf:counters+24", "pointer": "producer"},
                {"addr": "buf:counters+32", "pointer": "observed"},
                {"addr": "buf:producer", "bytes": word(2)},
                {"addr": "buf:observed", "bytes": word(1 if same else 0)},
            ]
            if available < 64:
                c["stubs"] = [{"address": "0x014712b0", "argc": 3, "ret": 0}]
            else:
                c["stubs"] = [{"address": "0x014784a0", "argc": 1, "ret": 0}]
                c["stubs"].append(
                    {"address": "0x02ab0ce0", "argc": 3, "argi": [6, 8], "ret": 0, "argmem": [{"arg": 2, "size": 64}]}
                )
            yield c


def cases():
    yield from base_cases()
    for remaining in (6, 7):
        c = next(c for c in base_cases() if c["id"] == "capacity_64_same_counter_0")
        c["id"] = f"post_prepare_remaining_{remaining}"
        c["stubs"][0]["writes"] = [
            {"arg": 0, "offset": 16, "pointer": f"payload+{256 - remaining * 4}", "size": 8},
        ]
        if remaining < 7:
            c["stubs"][1] = {"address": "0x014712b0", "argc": 3, "ret": 1}
        yield c
    c = next(c for c in base_cases() if c["id"] == "capacity_0_same_counter_1")
    c["id"] = "initial_capacity_successful_retry"
    c["stubs"][0]["writes"] = [
        {"arg": 0, "offset": 8, "pointer": "payload+256", "size": 8},
    ]
    c["stubs"] += [
        {"address": "0x014784a0", "argc": 1, "ret": 0},
        {"address": "0x02ab0ce0", "argc": 3, "ret": 0, "argmem": [{"arg": 2, "size": 64}]},
    ]
    yield c

    case = next(case for case in base_cases() if case["id"] == "capacity_64_same_counter_1")
    case["id"] = "delegate_changes_live_cursor"
    case["stubs"][-1]["writes"] = [{"arg": 0, "offset": 16, "pointer": "payload+32", "size": 8}]
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
            if address == "0x02ab0ce0":
                callback.setdefault("argmem", []).append({"arg": 0, "size": 8})
                callback.update(argi=[0, 1, 2, 6, 8], argu32=[7], argu8=[9])
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
