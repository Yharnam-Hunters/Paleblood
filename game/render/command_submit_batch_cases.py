#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: command_submit_batch_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ab0ce0"


def base_cases():
    for optional in (False, True):
        for first_size, second_size in ((0, 0), (4, 0), (0, 8), (12, 24)):
            c = basic(f"optional_{int(optional)}_sizes_{first_size}_{second_size}", returns="void", size=0xB520)
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
            c["args"].update({"rsi": "buf:commands" if optional else 0, "rdx": "buf:snapshot"})
            c["stack"] = ["0x89abcdef12345678", 3, "0x0123456789abcdef", 0x7F]
            c["memory"] = [
                {"addr": "buf:self+46224", "pointer": "owner"},
                {"addr": "buf:owner+46888", "pointer": "counters"},
                {"addr": "buf:counters+24", "pointer": "producer"},
                {"addr": "buf:counters+32", "pointer": "observed"},
                {"addr": "buf:producer", "bytes": word(1)},
                {"addr": "buf:observed", "bytes": word(0)},
                {"addr": "buf:commands", "pointer": "payload"},
                {"addr": "buf:commands+16", "pointer": f"payload+{first_size}"},
                {"addr": "buf:snapshot", "pointer": "payload+64"},
                {"addr": "buf:snapshot+16", "pointer": f"payload+{64+second_size}"},
            ]
            if not optional:
                c["stubs"] = [
                    {
                        "address": "0x02ab0e30",
                        "argc": 2,
                        "ret": 0,
                        "argmem": [{"arg": 0, "offset": 0, "size": 12}, {"arg": 0, "offset": 16, "size": 9}],
                    }
                ]
            else:
                n = int(first_size != 0) + int(second_size != 0)
                if n:
                    c["stubs"] = [
                        {
                            "address": "0x01489a20",
                            "argc": 9,
                            "ret": 0,
                            "argmem": [{"arg": 1, "size": n * 8}, {"arg": 2, "size": n * 4}],
                        },
                        {"address": "0x01489b50", "argc": 0, "ret": 0},
                    ]
            yield c


def cases():
    yield from base_cases()
    for producer, observed in ((2, 0), (2, 0x80000000), (0, 0), (0x90000000, 0x80000000)):
        c = next(c for c in base_cases() if c["id"] == "optional_1_sizes_12_24")
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
    c = next(c for c in base_cases() if c["id"] == "optional_1_sizes_12_24")
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
            if address == "0x01489a20":
                callback.update(argi=[1, 2, 3, 4, 5, 8], argu32=[0, 6, 7])
            elif address == "0x02ab0ce0":
                callback.update(argi=[0, 1, 2, 6, 8], argu32=[7], argu8=[9])
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
