#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: set_depth_render_target_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x014739b0"


def cases():
    values = [0, 1, 7, 4294967295]
    modes = [
        ("direct", 80, 0, False),
        ("boundary", 24, 0, False),
        ("reject_zero", 0, 0, False),
        ("reject_high", 1, 0xABCDEF0000000100, False),
        ("accept_low_byte", 0, 0xABCDEF0000000001, False),
        ("accept_refresh_cursor", 0, 0x101, True),
    ]
    for value in values:
        c = packet_case(f"input_{value:016x}", value, "direct", 80, 0, False)
        yield c
    for label, available, result, redirect in modes[1:]:
        yield packet_case(label, values[-1], label, available, result, redirect)

    for kind in ("null", "zero", "second_only"):
        c = packet_case(f"selector_{kind}", 0, "direct", 80, 0, False)
        if kind == "null":
            c["args"]["rsi"] = 0
        else:
            c["buffers"]["descriptor"]["bytes"] = "00" * 64
            if kind == "second_only":
                c["memory"].append({"addr": "buf:descriptor+12", "bytes": word(0x12345678)})
        yield c

    # The clear path has a smaller reservation than a populated descriptor.
    # Keep a rejecting callback available even at the exact boundary: correct code
    # emits without calling it, while a one-word over-request must call and stop.
    for kind in ("null", "zero"):
        for available in (5, 6, 7):
            c = packet_case(f"selector_{kind}_capacity_{available}_reject_callback", 3, "clear", available, 0, False)
            if kind == "null":
                c["args"]["rsi"] = 0
            else:
                c["buffers"]["descriptor"]["bytes"] = "00" * 64
            yield c

    for destination in (0, 8):
        c = packet_case(f"selector_cursor_overlap_{destination}", 3, "direct", 80, 0, False)
        c["memory"] = [
            {"addr": "buf:self+8", "pointer": "self+4096"},
            {"addr": "buf:self+16", "pointer": f"self+{destination}"},
            {"addr": f"buf:descriptor+{8 if destination == 0 else 0}", "pointer": "other"},
        ]
        c["buffers"]["self"] = {"size": 8192, "bytes": "a5" * 8192}
        yield c

    # The source depth-view word aliases its destination packet header. It must
    # be loaded before that header is stored, while later descriptor reads stay live.
    c = packet_case("depth_view_source_overlaps_packet_header", 3, "direct", 80, 0, False)
    c["args"]["rsi"] = "buf:packet+20"
    c["buffers"]["packet"]["bytes"] = bytes((index * 37 + 11) & 0xFF for index in range(512)).hex()
    yield c


def packet_case(cid, value, label, available, result, redirect):
    c = basic(cid, returns="void", size=64)
    c["buffers"]["packet"] = {"size": 512, "bytes": "6b" * 512}
    c["buffers"]["other"] = {"size": 512, "bytes": "7c" * 512}
    c["args"].update({})
    c["args"]["rsi"] = "buf:descriptor"
    c["buffers"]["descriptor"] = {"size": 64, "bytes": (word(value) + bytes(range(5, 65)).hex())}
    c["memory"] = [
        {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
        {"addr": "buf:self+16", "pointer": "packet"},
        {"addr": "buf:self+24", "guest": "0x014712b0"},
        {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
    ]
    if available < 24:
        stub = {"address": "0x014712b0", "argc": 3, "ret": hex(result)}
        if redirect:
            stub["writes"] = [{"arg": 0, "offset": 16, "pointer": "other", "size": 8}]
        c["stubs"] = [stub]
    return c


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
