#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: configure_record_parameters_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x01496f60"


def cases():
    for x, y, end_x, end_y in [
        (0, 0, 0, 0),
        (0, 0, 1920, 1080),
        (100, 80, 4, 2),
        (0xFFFFFFFF, 0x80000000, 1, 0x7FFFFFFF),
        (8192, 16384, 32768, 65535),
    ]:
        for changed in (False, True):
            c = basic(f"coordinates_{x:x}_{y:x}_{end_x:x}_{end_y:x}_changed_{int(changed)}", returns="void", size=128)
            c["args"].update(
                {"rsi": x, "rdx": y, "rcx": end_x, "r8": end_y, "xmm0": word(0x7FC12345), "xmm1": word(0xFFC54321)}
            )
            c["memory"] = []
            t = {
                "address": "0x014765e0",
                "argc": 5,
                "argf32": [0, 1],
                "ret": 0,
                "argmem": [{"arg": 2, "size": 12}, {"arg": 3, "size": 12}],
            }
            if changed:
                t["writes"] = [
                    {"arg": 2, "bytes": word(0x3F800000) * 3, "size": 12},
                    {"arg": 3, "bytes": word(0x40800000) * 3, "size": 12},
                ]
            c["stubs"] = [
                t,
                {"address": "0x01485210", "argc": 1, "ret": 0, "writes": [{"arg": 0, "bytes": word(0), "size": 4}]},
                {
                    "address": "0x01485220",
                    "argc": 2,
                    "ret": 0,
                    "writes": [{"arg": 0, "bytes": word(0xA5A5), "size": 4}],
                },
                {"address": "0x01476980", "argc": 2, "ret": 0},
                {"address": "0x01476790", "argc": 5, "ret": 0},
                {"address": "0x01473650", "argc": 3, "ret": 0},
                {"address": "0x014736d0", "argc": 1, "argf32": [0, 1], "ret": 0},
                {"address": "0x01473750", "argc": 1, "argf32": [0, 1], "ret": 0},
            ]
            yield c

    # The viewport helper may update the borrowed triplets before the clamping and
    # guard-band calculations. Exercise both clamp axes and ordered float operands.
    float_edges = [
        ("below_clamp", 0.0, 8176.0),
        ("at_clamp", 0.0, 8192.0),
        ("above_clamp", 0.0, 8208.0),
        ("negative", 1.0, -8192.0),
        ("positive_infinity", float("inf"), float("inf")),
        ("negative_infinity", float("inf"), -float("inf")),
        ("zero", 0.0, 0.0),
        ("negative_zero", -0.0, -0.0),
    ]
    for label, scale, center in float_edges:
        c = next(cases())
        c["id"] = "viewport_helper_" + label
        c["args"].update(xmm0=word(0x3F000000), xmm1=word(0xBF800000))
        c["stubs"][0]["writes"] = [
            {"arg": 2, "bytes": struct.pack("<f", scale).hex() * 3, "size": 12},
            {"arg": 3, "bytes": struct.pack("<f", center).hex() * 3, "size": 12},
        ]
        yield c
    for scale_bits, center_bits in [
        (0x7FC12345, 0x7FC56789),
        (0xFFC12345, 0x7FC56789),
        (0x3F800000, 0x7FC54321),
        (0x7FC12345, 0x40800000),
    ]:
        c = next(cases())
        c["id"] = f"viewport_nan_{scale_bits:x}_{center_bits:x}"
        c["args"].update(xmm0=word(0), xmm1=word(0x3F800000))
        c["stubs"][0]["writes"] = [
            {"arg": 2, "bytes": word(scale_bits) * 3, "size": 12},
            {"arg": 3, "bytes": word(center_bits) * 3, "size": 12},
        ]
        yield c


    # Keep NaN classes and payloads independent: the first bound sees the scale
    # before it is quieted, while the second bound has already quieted it.
    # Exercise one axis at a time so a wrong minimum order has a named witness.
    for axis in ("x", "y"):
        for label, scale_bits, center_bits in (
            ("signaling_scale_quiet_center", 0x7FBFFFFF, 0x7FC00001),
            ("quiet_scale_signaling_center", 0x7FC00001, 0x7FBFFFFF),
        ):
            c = next(cases())
            c["id"] = "viewport_" + axis + "_" + label
            c["args"].update(xmm0=word(0), xmm1=word(0x3F800000))
            scales = [0x3F800000, 0x3F800000, 0x3F800000]
            centers = [0x40800000, 0x40800000, 0x40800000]
            index = 0 if axis == "x" else 1
            scales[index] = scale_bits
            centers[index] = center_bits
            c["stubs"][0]["writes"] = [
                {"arg": 2, "bytes": "".join(word(bits) for bits in scales), "size": 12},
                {"arg": 3, "bytes": "".join(word(bits) for bits in centers), "size": 12},
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
