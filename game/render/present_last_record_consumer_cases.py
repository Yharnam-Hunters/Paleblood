#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: present_last_record_consumer_cases.py OUT_DIR. Generate captures outside the repository.
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


def basic(cid, returns="void", size=512):
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


ADDRESS = "0x02ab6810"


def stub(address, argc, ret=0, write=False, mem=None, fp=None):
    s = {"address": address, "argc": argc, "ret": ret}
    if write:
        s["writes"] = [{"arg": 0, "bytes": word(0x12345678), "size": 4}]
    if mem:
        s["argmem"] = mem
    if fp:
        s["argf32"] = fp
    return s


def base_cases():
    for classification, secondary, resource, force, count in [
        (0, 0, 0, 0, 0),
        (2, 0, 1, 0, 0),
        (1, 1, 1, 0, 0),
        (1, 0, 0, 0, 0),
        (1, 0, 0, 1, 0),
        (1, 0, 0, 1, 2),
        (1, 0, 0, 0x100, 0),
    ]:
        c = basic(
            f"class_{classification}_secondary_{secondary}_resource_{resource}_force_{force}_count_{count}",
            returns="void",
            size=0xB740,
        )
        c["buffers"].update({"record": {"size": 64, "bytes": "72" * 64}, "payload": {"size": 64}})
        c["args"].update({"rsi": "buf:record", "rdx": "buf:payload", "rcx": "0xfffffffe", "r8": count, "r9": force})
        c["memory"] = [
            {"addr": "buf:payload+8", "bytes": qword(0x1234 if resource else 0)},
            {"addr": "buf:payload+20", "bytes": word(0x12345678)},
            {"addr": "buf:payload+37", "bytes": "20"},
        ]
        c["stubs"] = [stub("0x01472730", 1, 0 if classification != 1 or secondary else 1)]
        tracking = classification != 1 or secondary != 0
        if tracking:
            if resource:
                c["stubs"] += [stub("0x02abaeb0", 1, 10), stub("0x02abaed0", 1, 20)]
            yield c
            continue
        if (force & 0xFF) == 0:
            yield c
            continue
        c["stubs"] += [
            stub("0x02ad2c30", 2),
            stub("0x01475da0", 2),
            stub("0x01496f60", 5, fp=[0, 1]),
            stub("0x014726c0", 1, 1),
            stub("0x01477150", 3),
            stub("0x01485380", 1, write=True),
            stub("0x014854b0", 1, write=True),
            stub("0x014733a0", 2),
            stub("0x01473400", 2),
        ]
        c["stubs"] += [stub("0x02ad1e10", 4) for _ in range(8)]
        c["stubs"] += [
            stub("0x01473320", 3),
            stub("0x01485020", 1, write=True),
            stub("0x01485080", 2, write=True),
            stub("0x01485070", 2, write=True),
            stub("0x014769e0", 2),
            stub("0x02ace1e0", 4),
            stub("0x01473fe0", 2),
            stub("0x01475370", 2),
            stub("0x02ace5e0", 4),
        ]
        for _ in range(count):
            c["stubs"] += [
                stub("0x014721d0", 3, mem=[{"arg": 0, "size": 52}]),
                stub("0x02ad1e20", 3),
                stub("0x014741b0", 2),
            ]
        c["stubs"] += [
            stub("0x02acfc20", 2),
            stub("0x01473320", 3),
            stub("0x01485020", 1, write=True),
            stub("0x014769e0", 2),
        ]
        c["stubs"] += [stub("0x02ad1e30", 3) for _ in range(8)]
        c["stubs"] += [stub("0x02ad1e70", 2)]
        yield c


def cases():
    yield from base_cases()
    original_cases = list(base_cases())
    tracking = next(c for c in original_cases if c["id"].startswith("class_2_"))
    for minimum, maximum in [(0, 1), (0, 100), (15, 1), (15, 100), (10, 30)]:
        c = json.loads(json.dumps(tracking))
        c["id"] = f"tracked_range_min_{minimum}_max_{maximum}"
        c["memory"] += [
            {"addr": "buf:self+0xb48c", "bytes": word(minimum)},
            {"addr": "buf:self+0xb490", "bytes": word(maximum)},
        ]
        yield c
    admitted = next(c for c in original_cases if "force_1_count_0" in c["id"])
    for option in (0, 0x100):
        c = json.loads(json.dumps(admitted))
        c["id"] = f"commands_unsuppressed_option_{option:x}"
        c["args"]["r9"] = 0
        c["memory"] = [r for r in c["memory"] if r["addr"] != "buf:payload+37"]
        c["memory"].append({"addr": "buf:payload+37", "bytes": "00"})
        next(t for t in c["stubs"] if t["address"] == "0x014726c0")["ret"] = option
        yield c


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = []
    for original in cases():
        for flags in (0, 2, 0xA5A5A5A5, 0xFFFFFFFF):
            entry = json.loads(json.dumps(original))
            entry["id"] += f"_dirty_{flags:08x}"
            entry["memory"].append({"addr": "buf:self+0xb498", "bytes": word(flags)})
            generated.append(entry)
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
