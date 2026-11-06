#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic PA_SC_MODE_CNTL_0 flag and reservation cases, not game recordings."""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x01477150"
MASK64 = (1 << 64) - 1


def packet_case(cid, value, available, result=0, redirect=False, scissor=0xDEADBEEF):
    """Build a command buffer and independently supply both packed flags."""
    case = {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": "void",
        "args": {"rdi": "buf:self", "rsi": hex(value), "rdx": hex(scissor)},
        "buffers": {
            "self": {"size": 64, "bytes": "a5" * 64},
            "packet": {"size": 512, "bytes": "6b" * 512},
            "other": {"size": 512, "bytes": "7c" * 512},
        },
        "memory": [
            {"addr": "buf:self+8", "pointer": f"packet+{available * 4}"},
            {"addr": "buf:self+16", "pointer": "packet"},
            {"addr": "buf:self+24", "guest": "0x014712b0"},
            {"addr": "buf:self+32", "bytes": struct.pack("<Q", 0x123456789ABCDEF0 & MASK64).hex()},
        ],
        "stubs": [],
    }
    if available < 3:
        stub = {"address": "0x014712b0", "argc": 3, "ret": hex(result)}
        if redirect:
            stub["writes"] = [{"arg": 0, "offset": 16, "pointer": "other", "size": 8}]
        case["stubs"] = [stub]
    return case


def cases():
    """Keep reservation coverage while varying each flag independently."""
    values = [0, 1, 127, 256, 305419896, 4294967295, 12379813733990400254]
    for value in values:
        yield packet_case(f"input_{value:016x}", value, 80)
    for label, available, result, redirect in [
        ("boundary", 3, 0, False),
        ("reject_zero", 0, 0, False),
        ("reject_high", 1, 0xABCDEF0000000100, False),
        ("accept_low_byte", 0, 0xABCDEF0000000001, False),
        ("accept_refresh_cursor", 0, 0x101, True),
    ]:
        yield packet_case(label, values[-1], available, result, redirect)
    for label, msaa, scissor in [
        ("scissor_zero", 0, 0),
        ("scissor_bit_one_only", 0, 2),
        ("msaa_bit_one_without_scissor", 2, 0),
    ]:
        yield packet_case(label, msaa, 80, scissor=scissor)


def main():
    """Write deterministic synthetic cases to the requested directory."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for case in generated:
        (destination / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
