#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic YEBIS allocator-selection cases. Usage: SCRIPT OUT_DIR.

All global setup and callback writes stay in the original writable data image.
Logger scripts observe only the arguments supplied by each actual call. Their
fixed global writes use an offset from that call's known format argument, as
required by the scripted game-function callback schema. No allocator is called.
"""
import copy
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x00fb6e60"
ALLOCATION_COUNT = 0x05866700
CURRENT_ALLOCATOR = 0x056b3618
DEFAULT_ALLOCATOR = "0x056b3610"
DIAGNOSTICS_GATE = 0x05519a68
DETAIL_GATE = 0x05519a70
LOG_MESSAGE = "0x00fb1a90"
SOURCE_FORMAT = 0x04bfec19
MESSAGE_FORMAT = 0x04bfec6b


def word(value):
    """Encode the complete observed DWORD, including noncanonical nonzero bits."""
    return struct.pack("<I", value)


def case(cid, count=7, gate=2, detail=3, allocator="buf:new-allocator+16",
         count_neighbor=0x55aa55aa, gate_neighbor=0x12345678,
         detail_neighbor=0x87654321, first_writes=(), second_writes=(),
         assert_writes=(), log_results=("0x1357", "0x2468"),
         assertion_result="0xabcdef0123456789", incoming=None, stack=()):
    """Observe publication, both logger calls and a possible returning assertion."""
    assert all(0 <= value <= 0xffffffff for value in
               [count, gate, detail, count_neighbor, gate_neighbor, detail_neighbor])
    args = {"rdi": allocator, "rsi": "0xfedcba9876543210", "rdx": "0x100000000",
            "rcx": "0x8000000000000000", "r8": "0xffffffffffffffff", "r9": "0x1",
            "rax": "0x123456789abcdef0"}
    args.update(incoming or {})

    def script(format_address, changes, result, source_line=False):
        writes = []
        for address, data in changes:
            assert address >= format_address
            writes.append({"arg": 0, "offset": address - format_address,
                           "bytes": data.hex()})
        entry = {"address": LOG_MESSAGE, "argc": 2 if source_line else 1,
                 "argi": [0], "ret": result, "writes": writes}
        if source_line:
            entry["argu32"] = [1]
        return entry

    row = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
           "args": args,
           "buffers": {"new-allocator": {"size": 64, "bytes": "a5" * 64},
                       "old-allocator": {"size": 64, "bytes": "b6" * 64}},
           "memory": [{"addr": hex(ALLOCATION_COUNT),
                       "bytes": struct.pack("<II", count, count_neighbor).hex()},
                      {"addr": hex(DIAGNOSTICS_GATE),
                       "bytes": struct.pack("<II", gate, gate_neighbor).hex()},
                      {"addr": hex(DETAIL_GATE),
                       "bytes": struct.pack("<II", detail, detail_neighbor).hex()},
                      {"addr": hex(CURRENT_ALLOCATOR), "pointer": "old-allocator+16"},
                      {"addr": hex(CURRENT_ALLOCATOR + 8), "bytes": "c7" * 8}],
           "stubs": [script(SOURCE_FORMAT, first_writes, log_results[0], True),
                     script(MESSAGE_FORMAT, second_writes, log_results[1])],
           "imports": [{"name": "_Assert", "argc": 2, "argi": [0, 1],
                        "ret": assertion_result,
                        "writes": [{"addr": hex(address), "bytes": data.hex()}
                                   for address, data in assert_writes]}]}
    if stack:
        row["stack"] = list(stack)
    return row


def cases():
    """Distinguish DWORD gates, pointer publication and the post-logging re-read."""
    result = []
    for count in [0, 1, 0x100, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff]:
        for gate, detail in [(0, 0), (1, 0), (0, 1), (1, 1)]:
            for name, pointer in [("default", "0x0"), ("custom", "buf:new-allocator+16")]:
                result.append(case(f"fields-{count:08x}-{gate}-{detail}-{name}",
                                   count, gate, detail, pointer))
    for field in ["gate", "detail"]:
        for value in [0x100, 0x10000, 0x80000000, 0xffffffff]:
            values = {"count": 2, "gate": 1, "detail": 1, field: value}
            result.append(case(f"wide-{field}-{value:08x}", **values))
    for neighbor in [0, 0xffffffff]:
        result.append(case(f"count-neighbor-{neighbor:08x}", count=0,
                           count_neighbor=neighbor))
        result.append(case(f"gate-neighbor-{neighbor:08x}", gate=0,
                           gate_neighbor=neighbor))
        result.append(case(f"detail-neighbor-{neighbor:08x}", detail=0,
                           detail_neighbor=neighbor))
    for name, pointer in [("low", "0x1"), ("high-only", "0x100000000"),
                          ("top-bit", "0x8000000000000000"),
                          ("all-bits", "0xffffffffffffffff"),
                          ("same-current", "buf:old-allocator+16"),
                          ("current-slot", "guest:0x056b3618"),
                          ("count-slot", "guest:0x05866700"),
                          ("builtin", "guest:" + DEFAULT_ALLOCATOR)]:
        for count in [0, 3]:
            result.append(case(f"pointer-{name}-{count}", count=count, allocator=pointer))
    for after in [0, 1, 0x100, 0x10000, 0x80000000, 0xffffffff]:
        for index, label in enumerate(["first", "second"]):
            values = {"first_writes": (), "second_writes": ()}
            values[label + "_writes"] = [(ALLOCATION_COUNT, word(after))]
            result.append(case(f"{label}-log-count-{after:08x}", count=5, **values))
    for before, after in [(0, 1), (0, 0x100), (1, 0), (0x80000000, 0)]:
        result.append(case(f"both-logs-count-{before:08x}-{after:08x}", count=5,
                           first_writes=[(ALLOCATION_COUNT, word(before))],
                           second_writes=[(ALLOCATION_COUNT, word(after))]))
    for field, address in [("gate", DIAGNOSTICS_GATE), ("detail", DETAIL_GATE)]:
        result.append(case("first-log-clears-" + field,
                           first_writes=[(address, word(0))]))
    marker = struct.pack("<Q", 0x1234567800000001)
    for label in ["first", "second", "assert"]:
        values = {label + "_writes": [(CURRENT_ALLOCATOR, marker)]}
        result.append(case(label + "-keeps-allocator-write", **values))
    for after in [0, 0x100, 0xffffffff]:
        result.append(case(f"assert-count-{after:08x}", gate=0,
                           assert_writes=[(ALLOCATION_COUNT, word(after))]))
    for index, value in enumerate(["0x0", "0x1", "0x100000000", "0xffffffffffffffff"]):
        result.append(case(f"ignored-first-log-result-{index}", log_results=(value, "0x2468")))
        result.append(case(f"ignored-second-log-result-{index}", log_results=("0x1357", value)))
        result.append(case(f"ignored-assert-result-{index}", gate=0, assertion_result=value))
    for reg in ["rax", "rsi", "rdx", "rcx", "r8", "r9"]:
        for name, value in [("zero", "0x0"), ("poison", "0x13579bdf2468ace0")]:
            for count in [0, 9]:
                result.append(case(f"ignored-{reg}-{name}-{count}", count=count,
                                   incoming={reg: value}))
    result.append(case("ignored-stack-zero", stack=["0x0"] * 4))
    result.append(case("ignored-stack-poison", stack=["0xffffffffffffffff", "0x100000000",
                                                      "buf:new-allocator+16", "0x8000000000000000"]))
    return result


def main():
    """Write deterministic fixtures and reject duplicate payloads as well as IDs."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = cases()
    assert len({row["id"] for row in generated}) == len(generated)
    payloads = []
    for row in generated:
        payload = copy.deepcopy(row)
        del payload["id"]
        payloads.append(json.dumps(payload, sort_keys=True))
    assert len(set(payloads)) == len(payloads)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
