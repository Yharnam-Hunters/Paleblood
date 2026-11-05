#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: emit_present_release_packet_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x014778f0"


def cases():
    values = [0, 1, 63, 64, 4294967295]
    modes = [
        ("direct", 80, 0, False),
        ("boundary", 6, 0, False),
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

    c = packet_case("stack_mode_bit_two_is_masked", 1, "direct", 80, 0, False)
    # Stack-mode bit 2 and flags bit 4 both contribute to control bit 27.
    # Leave the independent flag clear so it cannot hide a widened mode mask.
    c["args"]["rdx"] = 1
    c["stack"][1] = 4
    yield c
    c = packet_case("stack_mode_retains_low_bits", 1, "direct", 80, 0, False)
    c["args"]["rdx"] = 1
    c["stack"][1] = 6
    yield c
    c = packet_case("stack_mode_overlaps_control_flag", 1, "direct", 80, 0, False)
    # With the flag set, the same widened-mask mistake is deliberately hidden.
    c["stack"][1] = 4
    yield c
    c = packet_case("packet_overwrites_live_cursor", 1, "direct", 80, 0, False)
    # The packet's data words overwrite the buffer cursor before its increment.
    c["memory"][0] = {"addr": "buf:self+8", "pointer": "self+64"}
    c["memory"][1] = {"addr": "buf:self+16", "pointer": "self"}
    yield c


def packet_case(cid, value, label, available, result, redirect):
    c = basic(cid, returns="i64", size=64)
    c["buffers"]["packet"] = {"size": 512, "bytes": "6b" * 512}
    c["buffers"]["other"] = {"size": 512, "bytes": "7c" * 512}
    c["args"].update({"rdx": "0x11", "rcx": "0x123456789abcdef7", "r8": 3, "r9": "0xfedcba9876543210"})
    c["args"]["rsi"] = hex(value)
    c["stack"] = ["0x89abcdef12345678", "0x0123456789abcdef"]
    c["memory"] = [
        {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
        {"addr": "buf:self+16", "pointer": "packet"},
        {"addr": "buf:self+24", "guest": "0x014712b0"},
        {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
    ]
    if available < 6:
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
