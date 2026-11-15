#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic opaque-draw edge cases; these are not game recordings.

Usage: emit_draw_opaque_auto_cases.py OUT_DIR [--profile PROFILE].
The default edge profile writes 95 cases. Both trace profiles select the 18
alias cases; trace-native-query adds an explicit synthetic in-image query
oracle. Keep all generated cases and patches private.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402

ADDRESS = "0x01474250"
CAPACITY_CALLBACK = "0x014712b0"
USER_PA_QUERY = "0x01489c40"
MASK64 = (1 << 64) - 1
assert PAGE >= 1024 and SANDBOX & 0xFFFFFFFF == 0


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def packet_case(cid, *, flags=0, query=0, available=5, callback_result=0,
                self_offset=0, output="packet", output_offset=0, redirect=False,
                callback_flags=None):
    state = f"self+{self_offset}"
    cursor = f"{output}+{output_offset}"
    limit_offset = output_offset + available * 4
    limit = {"pointer": f"{output}+{limit_offset}"}
    if not 0 <= limit_offset <= 1024:
        # The harness places sorted 1024-byte buffers at two-page intervals,
        # including each guard page. Only the small packet is accessed; a limit
        # used solely in integer arithmetic needs no huge backing allocation.
        buffer_index = ("other", "packet", "self").index(output)
        limit = {"bytes": qword(SANDBOX + buffer_index * 2 * PAGE + limit_offset)}
    case = {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:{state}"},
        "buffers": {name: {"size": 1024, "bytes": fill * 1024}
                    for name, fill in (("self", "a5"), ("packet", "6b"), ("other", "7c"))},
        "memory": [
            {"addr": f"buf:self+{self_offset + 8}", **limit},
            {"addr": f"buf:self+{self_offset + 16}", "pointer": cursor},
            {"addr": f"buf:self+{self_offset + 24}", "guest": CAPACITY_CALLBACK},
            {"addr": f"buf:self+{self_offset + 32}", "bytes": qword(0x123456789ABCDEF0)},
            {"addr": f"buf:self+{self_offset + 40}", "bytes": word(flags)},
        ],
        "stubs": [],
    }
    # The native comparison uses the low DWORD after an unsigned 64-bit shift.
    capacity = available & 0xFFFFFFFF
    if capacity < 5:
        callback = {"address": CAPACITY_CALLBACK, "argc": 3, "ret": hex(callback_result)}
        writes = []
        if redirect:
            writes.append({"arg": 0, "offset": 16, "pointer": "other+32", "size": 8})
        if callback_flags is not None:
            writes.append({"arg": 0, "offset": 40, "bytes": word(callback_flags)})
        if writes:
            callback["writes"] = writes
        case["stubs"].append(callback)
    if capacity >= 5 or callback_result & 0xFF:
        case["stubs"].append({"address": USER_PA_QUERY, "argc": 0, "ret": hex(query)})
    return case


def cases():
    for flags in (0, 1, 2, 3, 0xFFFFFFFC, 0xFFFFFFFF):
        for query in (0, 1, 0x100, 0x101, MASK64):
            yield packet_case(f"flags_{flags:08x}_query_{query:016x}", flags=flags, query=query)

    for available in (0, 1, 4, 5, 6, 0xFFFFFFFF, 1 << 32, (1 << 32) + 4, (1 << 32) + 5):
        for result in (0, 1, 0xABCDEF0000000100, 0xABCDEF0000000101):
            yield packet_case(f"capacity_{available:x}_answer_{result:016x}", available=available,
                              callback_result=result, flags=3, query=1)

    for offset in (0, 1, 3, 7):
        for flags in (0, 3):
            yield packet_case(f"callback_redirect_unaligned_{offset}_flags_{flags}", self_offset=offset,
                              available=0, callback_result=0x101, redirect=True,
                              callback_flags=flags, flags=flags ^ 3, query=1)

    # With output at self+8, DRAW's fixed initiator replaces the cursor's low
    # DWORD. Its live ADD routes the tail into owned other+0x4e in the harness
    # layout above. The other destinations exercise tail/cursor and flags aliases.
    for offset in (0, 4, 8, 24, 28, 32, 36, 40, 41):
        for query in (0, 1):
            yield packet_case(f"overlap_{offset}_query_{query}", output="self", output_offset=offset,
                              available=128, flags=3, query=query)

    for offset in (1, 3, 7):
        yield packet_case(f"unaligned_state_and_output_{offset}", self_offset=offset,
                          output_offset=offset, available=5, flags=3, query=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--profile", choices=("edge", "trace-scripted-query", "trace-native-query"), default="edge")
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    if any(out.glob("*.json")):
        parser.error("use an output directory without existing cases")
    entries = list(cases())
    if args.profile != "edge":
        entries = [case for case in entries if case["id"].startswith("overlap_")]
    if args.profile == "trace-native-query":
        # This is our test helper, not a recovered game function: MOV RAX,imm64;
        # MOV EAX,[RAX]; RET. Only the query is patched; the target body is intact.
        helper = b"\x48\xb8" + struct.pack("<Q", SANDBOX) + b"\x8b\x00\xc3"
        patch = ('<Patches><Metadata Name="synthetic-query-oracle"><Line Type="bytes" '
                 f'Address="{USER_PA_QUERY}" Value="{helper.hex()}"/></Metadata></Patches>')
        (out / "synthetic-query-oracle.xml").write_text(patch)
        for case in entries:
            query = [stub for stub in case["stubs"] if stub["address"] == USER_PA_QUERY]
            assert len(query) == 1
            value = int(query[0]["ret"], 0)
            case["stubs"] = [stub for stub in case["stubs"] if stub["address"] != USER_PA_QUERY]
            case["memory"].append({"addr": "buf:other", "bytes": word(value)})
    assert len({case["id"] for case in entries}) == len(entries)
    for case in entries:
        (out / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n")
    print(f"{len(entries)} synthetic {args.profile} cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
