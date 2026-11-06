#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: present_source_flush_state_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ad1f80"


def cases():
    for bank in (0, 1):
        for units in (0, 4, 8, 0x100000004):
            for dirty in (False, True):
                c = basic(f"bank_{bank}_units_{units:x}_dirty_{int(dirty)}", returns="void", size=512)
                c["buffers"]["self"]["bytes"] = "00" * 512
                c["buffers"].update({"context": {"size": 64}, "slot": {"size": 64}})
                c["args"].update({"rsi": "buf:context", "rdx": 0})
                c["memory"] = [
                    {"addr": "buf:self+326", "bytes": "01"},
                    memory(0x188, bank | (16 if dirty else 0)),
                    {"addr": f"buf:self+{0x60+bank*0x48}", "pointer": "slot"},
                    {"addr": "buf:self+256", "bytes": qword(0x123456789ABCDEF0)},
                    {"addr": "buf:slot+60", "bytes": word(5)},
                ]
                if dirty:
                    c["stubs"] = [
                        {"address": "0x02564ed0", "argc": 1, "ret": hex(units)},
                        {"address": "0x01473840", "argc": 3, "ret": 0, "argmem": [{"arg": 2, "size": 8}]},
                    ]
                yield c
    for bank in (0, 1):
        yield emit_mutation_case(f"bank_{bank}_emit_grows_count", bank, 1, new_count=2)
        yield emit_mutation_case(f"bank_{bank}_emit_shrinks_count", bank, 2, new_count=1)
        yield emit_mutation_case(f"bank_{bank}_emit_changes_pending_value", bank, 1, new_value=0xFEDCBA9876543210)

    for bank in (0, 1):
        yield emit_mutation_case(f"bank_{bank}_emit_changes_upper_flags", bank, 2, new_flags=0xA1B2C3F0 | bank)

    for bank in (0, 1):
        c = emit_mutation_case(f"bank_{bank}_all_ones_slot", bank, 1)
        for preset in c["memory"]:
            if preset["addr"] == f"buf:self+{0x60 + bank * 0x48}":
                preset.pop("pointer", None)
                preset["bytes"] = qword(MASK64)
        c["stubs"] = []
        yield c


    for bank in (0, 1):
        case = basic(f"bank_{bank}_slot_eight_dirty_bit_is_ignored", returns="void", size=512)
        case["buffers"]["self"]["bytes"] = "00" * 512
        case["buffers"].update({"context": {"size": 64}, "slot": {"size": 64}})
        case["args"].update({"rsi": "buf:context", "rdx": 0})
        case["memory"] = [
            {"addr": "buf:self+326", "bytes": "09"},
            memory(0x188, bank | 0x1000),
            {"addr": f"buf:self+{0x60 + bank * 0x48 + 8 * 8}", "pointer": "slot"},
            {"addr": "buf:slot+60", "bytes": word(5)},
        ]
        # The full-width dirty-mask mutant can complete both calls. The original
        # byte mask skips slot eight, so its empty trace is independently observable.
        case["stubs"] = [
            {"address": "0x02564ed0", "argc": 1, "ret": 8},
            {"address": "0x01473840", "argc": 3, "ret": 0, "argmem": [{"arg": 2, "size": 8}]},
        ]
        yield case


def emit_mutation_case(cid, bank, count, new_count=None, new_value=None, new_flags=None):
    command_offset = 0x10
    pending_values_offset = 0x100
    slot_count_offset = 0x146
    slot_bank_offset = 0x60 + bank * 0x48
    value_bytes = 8
    first_value = 0x123456789ABCDEF0
    second_value = 0x1122334455667788

    c = basic(cid, returns="void", size=512)
    c["buffers"]["self"]["bytes"] = "00" * 512
    c["buffers"]["slot"] = {"size": 64}
    # Deliberately alias the context's unused prefix with state backing. The emit
    # helper receives self+0x10, so its scripted writes can change caller state
    # through that pointer without adding a special harness operation.
    c["args"].update({"rsi": "buf:self", "rdx": 0})
    c["memory"] = [
        {"addr": f"buf:self+{slot_count_offset}", "bytes": f"{count:02x}"},
        memory(0x188, bank | 0x30),
        {"addr": f"buf:self+{slot_bank_offset}", "pointer": "slot"},
        {"addr": f"buf:self+{slot_bank_offset + value_bytes}", "pointer": "slot"},
        {"addr": f"buf:self+{pending_values_offset}", "bytes": qword(first_value)},
        {"addr": f"buf:self+{pending_values_offset + value_bytes}", "bytes": qword(second_value)},
        {"addr": "buf:slot+60", "bytes": word(5)},
    ]
    writes = []
    if new_count is not None:
        writes.append({"arg": 0, "offset": slot_count_offset - command_offset, "bytes": f"{new_count:02x}", "size": 1})
    if new_value is not None:
        writes.append(
            {"arg": 0, "offset": pending_values_offset - command_offset, "bytes": qword(new_value), "size": value_bytes}
        )

    if new_flags is not None:
        writes.append({"arg": 0, "offset": 0x188 - command_offset, "bytes": word(new_flags), "size": 4})

    # Both count variants include a complete second call script. A stale count
    # therefore changes the trace and state instead of causing an unscripted call.
    for index in range(max(count, new_count if new_count is not None else count)):
        c["stubs"].append({"address": "0x02564ed0", "argc": 1, "ret": 8})
        emit = {"address": "0x01473840", "argc": 3, "ret": 0, "argmem": [{"arg": 2, "size": value_bytes}]}
        if index == 0:
            emit["writes"] = writes
        c["stubs"].append(emit)
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
