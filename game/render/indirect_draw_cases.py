#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic indirect-draw edges; never gameplay recordings.

Usage: indirect_draw_cases.py OUT_DIR [--function FUNCTION] [--profile PROFILE].
The function defaults to OUT_DIR's name. The trace-native-query profile uses
an explicit synthetic in-image query oracle and excludes capacity callbacks.
Keep generated fixtures and patches outside the repository.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import EBOOT_BASE, IMAGE, PAGE, SANDBOX  # noqa: E402

FUNCTIONS = {"render_emit_draw_indirect": "0x014744d0",
             "render_emit_draw_index_indirect": "0x014746f0"}
TABLE = 0x04B2BF30
CAPACITY_CALLBACK = "0x014712b0"
QUERY = "0x01489c40"
MASK64 = (1 << 64) - 1
assert PAGE >= 1024 and SANDBOX & 0xFFFFFFFF == 0


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def owned_address(pointer):
    name, offset = pointer.split("+")
    return SANDBOX + ("other", "packet", "self").index(name) * 2 * PAGE + int(offset)


def packet_case(function, cid, *, data_offset=32, bank=0, first=1, second=2,
                table_value=None, table_target="other+128", flags=3, query=0,
                available=7, callback_result=0, self_offset=0, output="packet",
                output_offset=0, redirect=False, callback_flags=None, callback_table=None):
    limit_offset = output_offset + available * 4
    limit = {"pointer": f"{output}+{limit_offset}"}
    if not 0 <= limit_offset <= 1024:
        limit = {"bytes": qword(owned_address(f"{output}+0") + limit_offset)}
    if table_value is not None:
        # Exercise full-width address arithmetic and arbitrary table words through
        # owned RAM. The original image's read-only table is never modified.
        delta = owned_address(table_target) - (IMAGE + TABLE - EBOOT_BASE)
        assert delta >= 0 and delta % 4 == 0 and delta // 4 <= 0xFFFFFFFF
        bank = delta // 4
    case = {
        "schema": 1, "address": FUNCTIONS[function], "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{self_offset}", "rsi": hex(data_offset),
                 "rdx": hex(bank), "rcx": hex(first), "r8": hex(second)},
        "buffers": {name: {"size": 1024, "bytes": fill * 1024}
                    for name, fill in (("self", "a5"), ("packet", "6b"), ("other", "7c"))},
        "memory": [
            {"addr": f"buf:self+{self_offset + 8}", **limit},
            {"addr": f"buf:self+{self_offset + 16}", "pointer": f"{output}+{output_offset}"},
            {"addr": f"buf:self+{self_offset + 24}", "guest": CAPACITY_CALLBACK},
            {"addr": f"buf:self+{self_offset + 32}", "bytes": qword(0x123456789ABCDEF0)},
            {"addr": f"buf:self+{self_offset + 40}", "bytes": word(flags)},
        ], "stubs": [],
    }
    if table_value is not None:
        case["memory"].append({"addr": "buf:" + table_target, "bytes": word(table_value)})
    if (available & 0xFFFFFFFF) < 7:
        callback = {"address": CAPACITY_CALLBACK, "argc": 3, "argi": [0, 2],
                    "argu32": [1], "ret": hex(callback_result)}
        writes = []
        if redirect:
            writes.append({"arg": 0, "offset": 16, "pointer": "other+32", "size": 8})
        if callback_flags is not None:
            writes.append({"arg": 0, "offset": 40, "bytes": word(callback_flags)})
        if callback_table is not None:
            assert table_target == f"self+{self_offset + 64}"
            writes.append({"arg": 0, "offset": 64, "bytes": word(callback_table)})
        if writes:
            callback["writes"] = writes
        case["stubs"].append(callback)
    if (available & 0xFFFFFFFF) >= 7 or callback_result & 0xFF:
        case["stubs"].append({"address": QUERY, "argc": 0, "ret": hex(query)})
    return case


def cases(function):
    for base in (0, 0x2C00, 0xFFFF, 0xFFFFFFFF, 0x12345678):
        for first, second in ((0, 0), (0, 1), (1, 0), (1, 2), (0xFF, 0x80),
                              (0x100, 0x101), (0xABCDEF0000000101, 0xFFFFFFFF), (2, 1)):
            yield packet_case(function, f"base_{base:08x}_selectors_{first:x}_{second:x}",
                              table_value=base, first=first, second=second)
    for value in (0, 1, 3, 4, 0x80000000, 0xFFFFFFFF, 0xABCDEF0100000007):
        yield packet_case(function, f"offset_{value:016x}", data_offset=value)
    for bank in (1, 2, 3, 4, 5, 6, 0xABCDEF0100000003):
        yield packet_case(function, f"bank_{bank:016x}", bank=bank)
    for bank in (2, 4, 6):
        yield packet_case(function, f"bank_{bank}_zero_selectors", bank=bank, first=0, second=0)
    for flags in (0, 1, 2, 3, 0xFFFFFFFC, 0xFFFFFFFF):
        for query in (0, 1, 2, 0x100, 0x101, MASK64):
            yield packet_case(function, f"flags_{flags:08x}_query_{query:016x}",
                              flags=flags, query=query)
    for available in (0, 1, 6, 7, 8, 0xFFFFFFFF, 1 << 32, (1 << 32) + 6, (1 << 32) + 7):
        for result in (0, 1, 0xABCDEF0000000100, 0xABCDEF0000000101):
            yield packet_case(function, f"capacity_{available:x}_answer_{result:016x}",
                              available=available, callback_result=result, query=1)
    for offset in (0, 1, 3, 7):
        for flags in (0, 3):
            yield packet_case(function, f"callback_redirect_{offset}_flags_{flags}",
                              self_offset=offset, available=0, callback_result=0x101,
                              redirect=True, flags=flags ^ 3, callback_flags=flags, query=1)
    yield packet_case(function, "callback_replaces_table_word", available=0, callback_result=1,
                      table_value=0x2C00, table_target="self+64", callback_table=0xFFFF,
                      redirect=True, query=1)
    for offset in (0, 8, 12, 24, 28, 32, 36, 40, 41):
        for query in (0, 1):
            yield packet_case(function, f"overlap_{offset}_query_{query}", output="self",
                              output_offset=offset, table_value=0x2C00,
                              first=9 if offset == 12 else 1,
                              second=9 if offset == 8 else 2, available=128, query=query)
    for start in (56, 60):
        for query in (0, 1):
            yield packet_case(function, f"tail_overlaps_prefix_{start}_query_{query}",
                              self_offset=64, output="self", output_offset=start,
                              available=128, query=query)
    yield packet_case(function, "table_overlaps_draw_and_flags", output="self", output_offset=40,
                      table_value=0x2C02, table_target="self+40", available=128, query=1)
    for offset in (1, 3, 7):
        yield packet_case(function, f"unaligned_state_and_output_{offset}", self_offset=offset,
                          output_offset=offset, query=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--function", choices=FUNCTIONS)
    parser.add_argument("--profile", choices=("edge", "trace-native-query"), default="edge")
    args = parser.parse_args()
    function = args.function or args.out.name
    if function not in FUNCTIONS:
        parser.error("use a function-named output directory or --function")
    args.out.mkdir(parents=True, exist_ok=True)
    if any(args.out.glob("*.json")):
        parser.error("use an output directory without existing cases")
    entries = list(cases(function))
    if args.profile == "trace-native-query":
        entries = [case for case in entries if all(stub["address"] == QUERY for stub in case["stubs"])]
        # Our own 13-byte helper: MOV RAX,SANDBOX; MOV EAX,[RAX]; RET.
        # Only the external query is patched; both original target bodies are untouched.
        helper = b"\x48\xb8" + struct.pack("<Q", SANDBOX) + b"\x8b\x00\xc3"
        patch = ('<Patches><Metadata Name="synthetic-query-oracle"><Line Type="bytes" '
                 f'Address="{QUERY}" Value="{helper.hex()}"/></Metadata></Patches>')
        (args.out / "synthetic-query-oracle.xml").write_text(patch)
        for case in entries:
            assert len(case["stubs"]) == 1
            value = int(case["stubs"][0]["ret"], 0)
            case["stubs"] = []
            case["memory"].append({"addr": "buf:other", "bytes": word(value)})
    assert len({case["id"] for case in entries}) == len(entries)
    for case in entries:
        (args.out / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n")
    print(f"{function}: {len(entries)} synthetic {args.profile} cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
