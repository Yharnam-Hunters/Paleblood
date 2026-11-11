#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic SET_BASE edge cases; these are not game recordings.

Usage: emit_set_base_cases.py OUT_DIR [--profile edge|trace].
The trace profile selects callback-free cases for complete native instruction tracing.
Keep generated cases outside the repository.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402

ADDRESS = "0x014742f0"
CAPACITY_CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1
assert PAGE >= 1024 and SANDBOX & 0xFFFFFFFF == 0


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def packet_case(cid, *, address=MASK64, flags=0, available=4, callback_result=0,
                self_offset=0, output="packet", output_offset=0, redirect=False,
                callback_flags=None):
    limit_offset = output_offset + available * 4
    limit = {"pointer": f"{output}+{limit_offset}"}
    if not 0 <= limit_offset <= 1024:
        # Only the packet is accessed. A limit used in wrapping integer arithmetic
        # needs no large allocation; sorted buffers occupy two-page intervals.
        buffer_index = ("other", "packet", "self").index(output)
        limit = {"bytes": qword(SANDBOX + buffer_index * 2 * PAGE + limit_offset)}
    case = {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{self_offset}", "rsi": hex(address)},
        "buffers": {name: {"size": 1024, "bytes": fill * 1024}
                    for name, fill in (("self", "a5"), ("packet", "6b"), ("other", "7c"))},
        "memory": [
            {"addr": f"buf:self+{self_offset + 8}", **limit},
            {"addr": f"buf:self+{self_offset + 16}", "pointer": f"{output}+{output_offset}"},
            {"addr": f"buf:self+{self_offset + 24}", "guest": CAPACITY_CALLBACK},
            {"addr": f"buf:self+{self_offset + 32}", "bytes": qword(0x123456789ABCDEF0)},
            {"addr": f"buf:self+{self_offset + 40}", "bytes": word(flags)},
        ],
        "stubs": [],
    }
    if available & 0xFFFFFFFF < 4:
        callback = {"address": CAPACITY_CALLBACK, "argc": 3,
                    "argi": [0, 2], "argu32": [1], "ret": hex(callback_result)}
        writes = []
        if redirect:
            writes.append({"arg": 0, "offset": 16, "pointer": "other+32", "size": 8})
        if callback_flags is not None:
            writes.append({"arg": 0, "offset": 40, "bytes": word(callback_flags)})
        if writes:
            callback["writes"] = writes
        case["stubs"].append(callback)
    return case


def cases():
    for address in (0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFFFFFFFF, 0x100000000,
                    0xFF12345678, 0x10012345678, 0xABCDEF123456789F, MASK64):
        yield packet_case(f"address_{address:016x}", address=address)
    for flags in (0, 1, 2, 3, 4, 0x80000002, 0xFFFFFFFF):
        yield packet_case(f"flags_{flags:08x}", flags=flags)
    for available in (0, 1, 3, 4, 5, 0xFFFFFFFF, 1 << 32, (1 << 32) + 3, (1 << 32) + 4):
        for result in (0, 1, 0xABCDEF0000000100, 0xABCDEF0000000101):
            yield packet_case(f"capacity_{available:x}_answer_{result:016x}", available=available,
                              callback_result=result, flags=3)
    for offset in (0, 1, 3, 7):
        for flags in (0, 2):
            yield packet_case(f"callback_redirect_unaligned_{offset}_flags_{flags}",
                              self_offset=offset, available=0, callback_result=0x101,
                              redirect=True, callback_flags=flags, flags=flags ^ 2)
    for offset in (0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 41):
        yield packet_case(f"overlap_{offset}", output="self", output_offset=offset,
                          available=128, flags=3)
    for offset in (1, 3, 7):
        yield packet_case(f"unaligned_state_and_output_{offset}", self_offset=offset,
                          output_offset=offset, flags=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--profile", choices=("edge", "trace"), default="edge")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if any(args.out.glob("*.json")):
        parser.error("use an output directory without existing cases")
    entries = list(cases())
    if args.profile == "trace":
        entries = [case for case in entries if not case["stubs"]]
    assert len({case["id"] for case in entries}) == len(entries)
    for case in entries:
        (args.out / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n")
    print(f"{len(entries)} synthetic {args.profile} cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
