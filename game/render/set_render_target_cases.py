#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: set_render_target_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x014738b0"


def cases():
    values = [0, 1, 7, 4294967295]
    modes = [
        ("direct", 80, 0, False),
        ("boundary", 15, 0, False),
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

    for kind in ("null", "zero"):
        c = packet_case(f"clear_{kind}", 3, "direct", 80, 0, False)
        if kind == "null":
            c["args"]["rdx"] = 0
        else:
            c["buffers"]["descriptor"]["bytes"] = "00" * 64
        yield c
    c = packet_case("record_packet_changes_live_cursor", 3, "direct", 80, 0, False)
    c["buffers"]["self"]["size"] = 512
    c["buffers"]["self"]["bytes"] = "a5" * 512
    c["memory"][0] = {"addr": "buf:self+8", "pointer": "self+512"}
    c["memory"][1] = {"addr": "buf:self+16", "pointer": "self"}
    # The header and register index occupy the first two words. The color target
    # register pairs overwrite the command object's limit and cursor words.
    # The second QWORD source becomes a valid replacement trailing cursor.
    c["memory"].append({"addr": "buf:descriptor+8", "pointer": "packet+128"})
    yield c

    # The clear path has a smaller reservation than a populated descriptor.
    # Keep a rejecting callback available even at the exact boundary: correct code
    # emits without calling it, while a one-word over-request must call and stop.
    for kind in ("null", "zero"):
        for available in (2, 3, 4):
            c = packet_case(f"clear_{kind}_capacity_{available}_reject_callback", 3, "clear", available, 0, False)
            if kind == "null":
                c["args"]["rdx"] = 0
            else:
                c["buffers"]["descriptor"]["bytes"] = "00" * 64
            yield c

    # Distinct source words expose a late base/pitch read after the overlapping
    # slice/view store; a uniform packet fill would make those values equal.
    c = packet_case("color_base_source_overlaps_slice_view_output", 3, "direct", 80, 0, False)
    c["args"]["rdx"] = "buf:packet+16"
    c["buffers"]["packet"]["bytes"] = bytes((index * 17 + 3) & 0xFF for index in range(512)).hex()
    yield c


def packet_case(cid, value, label, available, result, redirect):
    c = basic(cid, returns="void", size=64)
    c["buffers"]["packet"] = {"size": 512, "bytes": "6b" * 512}
    c["buffers"]["other"] = {"size": 512, "bytes": "7c" * 512}
    c["args"].update({})
    c["args"].update({"rsi": hex(value), "rdx": "buf:descriptor"})
    c["buffers"]["descriptor"] = {"size": 64, "bytes": bytes(range(1, 65)).hex()}
    c["memory"] = [
        {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
        {"addr": "buf:self+16", "pointer": "packet"},
        {"addr": "buf:self+24", "guest": "0x014712b0"},
        {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
    ]
    if available < 15:
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
