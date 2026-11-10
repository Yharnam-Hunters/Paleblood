#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: emit_draw_index_auto_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x014741b0"


def cases():
    values = [0, 1, 4294967295]
    modes = [
        ("direct", 80, 0, False),
        ("boundary", 5, 0, False),
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

    for next_offset in (12, 16):
        c = packet_case(f"fallback_overlaps_cursor_{next_offset}", 0, "direct", 80, 0, False)
        c["buffers"]["self"]["size"] = 512
        c["buffers"]["self"]["bytes"] = "a5" * 512
        # Arrange the first DRAW packet's live cursor ADD to land on self.
        c["memory"][1] = {"addr": "buf:self+16", "pointer": "self+0"}
        # DRAW stores hit offsets 0, 4 and 8; cursor 16 stays live, then advances 12.
        # The offset 16 route moves the initial output four bytes.
        if next_offset == 16:
            c["memory"][1]["pointer"] = "self+4"
        c["memory"][0] = {"addr": "buf:self+8", "pointer": "self+512"}
        c["stubs"] = [{"address": "0x01489c40", "argc": 0, "ret": 0}]
        yield c

    for next_offset in (12, 16):
        c = packet_case(f"user_pa_overlaps_cursor_{next_offset}", 0, "direct", 80, 0, False)
        c["buffers"]["self"] = {"size": 512, "bytes": "a5" * 512}
        c["memory"][0] = {"addr": "buf:self+8", "pointer": "self+512"}
        c["memory"][1] = {"addr": "buf:self+16", "pointer": f"self+{next_offset - 12}"}
        c["stubs"] = [{"address": "0x01489c40", "argc": 0, "ret": 1}]
        yield c
    # DRAW's auto-index word overlaps flags at +0x28 and sets bit 1. A flags
    # snapshot taken before DRAW therefore cannot choose the trailing event header.
    c = packet_case("draw_overwrites_shader_flag_before_user_pa", 0, "direct", 80, 0, False)
    c["buffers"]["self"] = {"size": 512, "bytes": "a5" * 512}
    c["memory"][0] = {"addr": "buf:self+8", "pointer": "self+512"}
    c["memory"][1] = {"addr": "buf:self+16", "pointer": "self+32"}
    c["memory"].append(memory(40, 0))
    c["stubs"] = [{"address": "0x01489c40", "argc": 0, "ret": 1}]
    yield c

    # DRAW overlaps the cursor's low DWORD. Its live advance moves the trailing
    # event into the first owned buffer; both packet destinations remain valid.
    c = packet_case("draw_live_cursor_routes_to_owned_buffer", 0, "direct", 80, 0, False)
    c["buffers"]["self"] = {"size": 512, "bytes": "a5" * 512}
    c["memory"][0] = {"addr": "buf:self+8", "pointer": "self+512"}
    c["memory"][1] = {"addr": "buf:self+16", "pointer": "self+8"}
    c["memory"].append({"addr": "buf:self", "pointer": "other"})
    c["stubs"] = [{"address": "0x01489c40", "argc": 0, "ret": 1}]
    yield c


def packet_case(cid, value, label, available, result, redirect):
    c = basic(cid, returns="void", size=64)
    c["buffers"]["packet"] = {"size": 512, "bytes": "6b" * 512}
    c["buffers"]["other"] = {"size": 512, "bytes": "7c" * 512}
    c["args"].update({})
    c["args"]["rsi"] = hex(value)
    c["memory"] = [
        {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
        {"addr": "buf:self+16", "pointer": "packet"},
        {"addr": "buf:self+24", "guest": "0x014712b0"},
        {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
    ]
    if available < 5:
        stub = {"address": "0x014712b0", "argc": 3, "ret": hex(result)}
        if redirect:
            stub["writes"] = [{"arg": 0, "offset": 16, "pointer": "other", "size": 8}]
        c["stubs"] = [stub]
    if available >= 5 or result & 255:
        c["stubs"].append({"address": "0x01489c40", "argc": 0, "ret": value & 1})
    return c


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
