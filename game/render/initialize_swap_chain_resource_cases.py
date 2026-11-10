#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic resource-wrapper admission, allocator and cleanup cases.

Usage: initialize_swap_chain_resource_cases.py OUT_DIR. Helper effects are
scripted edge inputs, not recordings captured in a game run.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x02565f90"


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & ((1 << 64) - 1)).hex()


def base_case(stage):
    case = {
        "schema": 1,
        "address": ADDRESS,
        "id": stage,
        "returns": "i32",
        "args": {"rdi": "buf:self", "rsi": "buf:output", "rdx": "buf:parameters", "rcx": 0, "r8": 0, "r9": 0},
        "stack": ["0x123456789abcdef0"],
        "buffers": {
            "self": {"size": 0xDC40},
            "output": {"size": 16, "bytes": "a5" * 16},
            "parameters": {"size": 64},
            "owner": {"size": 16},
            "allocator": {"size": 16},
            "vtable": {"size": 128},
            "object": {"size": 96, "bytes": "a5" * 96},
        },
        "memory": [
            {"addr": "buf:self", "bytes": qword(1) + qword(1)},
            {"addr": "buf:self+32", "bytes": qword(1)},
            {"addr": "buf:self+56336", "pointer": "allocator"},
            {"addr": "buf:self+56352", "pointer": "owner"},
            {
                "addr": "buf:parameters+32",
                "bytes": word(stage == "rejected_primary") + word(0) + word(8 if stage == "rejected_secondary" else 0),
            },
            {"addr": "buf:owner", "pointer": "vtable"},
            {"addr": "buf:allocator", "pointer": "vtable"},
            {"addr": "buf:vtable+88", "guest": "0x014712b0"},
        ],
        "stubs": [
            {
                "address": "0x02571d60",
                "argc": 7,
                "ret": 12 if stage == "helper_failure" else 0,
                "writes": [{"arg": 0, "bytes": "00" * 16 + "".join(qword(v) for v in (1, 2, 3, 4, 5)), "size": 56}],
            },
            {"address": "0x014712b0", "argc": 3, "ret": 0 if stage == "allocation_failure" else "buf:object"},
        ],
    }
    return case


def resource_case(
    cid,
    *,
    kind=0,
    selection=0,
    flags=0,
    secondary=0,
    initial=False,
    zero_entry=False,
    zero_entry_index=0,
    count=1,
    count_parameter=1,
    width=0,
    height=0,
    kind_parameter=0,
    capability=True,
    override=False,
    allocation_failure=False,
):
    c = base_case("publish")
    c["id"] = cid
    c["memory"].extend(
        [
            {"addr": "buf:parameters", "bytes": word(width) + word(height)},
            {"addr": "buf:parameters+8", "bytes": word(count_parameter) + word(count)},
            {"addr": "buf:parameters+20", "bytes": word(kind_parameter)},
            {"addr": "buf:parameters+28", "bytes": word(kind) + word(flags) + word(selection) + word(secondary)},
        ]
    )
    if not capability:
        c["memory"].append({"addr": "buf:self+32", "bytes": qword(0)})
    if initial:
        derived_levels = count_parameter or max(1, width.bit_length(), height.bit_length())
        entry_count = (count * derived_levels) & 0xFFFFFFFF
        c["buffers"]["initial"] = {"size": max(256, (entry_count + 1) * 16)}
        c["args"]["rcx"] = "buf:initial"
        for index in range(entry_count):
            c["memory"].append(
                {
                    "addr": f"buf:initial+{index * 16 + 8}",
                    "bytes": word(0 if zero_entry and index == zero_entry_index else 0x12345678),
                }
            )
    if override:
        c["buffers"]["allocator_override"] = {"size": 16}
        c["buffers"]["owner_override"] = {"size": 16}
        c["args"].update(r8="buf:allocator_override", r9="buf:owner_override")
        c["memory"].extend(
            [
                {"addr": "buf:allocator_override", "pointer": "vtable"},
                {"addr": "buf:owner_override", "pointer": "vtable"},
            ]
        )
    # Keep the lower helper installed on rejected inputs as an observer. An
    # unexpected admission must produce a compared call, not run unstubbed code.
    if allocation_failure:
        c["stubs"][1]["ret"] = 0
        c["stubs"][0]["writes"] = [
            {"arg": 0, "pointer": "owner", "size": 8},
            {"arg": 0, "offset": 8, "pointer": "object", "size": 8},
        ]
        c["memory"].append({"addr": "buf:vtable+112", "guest": "0x01471290"})
        c["stubs"].append({"address": "0x01471290", "argc": 2, "ret": 0})
    return c


def cases():
    for stage in ("rejected_primary", "rejected_secondary", "helper_failure", "allocation_failure", "publish"):
        yield base_case(stage)
    specifications = [
        ("kind1_with_initial", dict(kind=1, flags=8, initial=True)),
        ("kind1_without_initial", dict(kind=1, flags=8)),
        ("kind1_zero_initial_entry", dict(kind=1, flags=8, initial=True, zero_entry=True)),
        ("kind2_enabled", dict(kind=2, selection=1, flags=8)),
        ("kind2_disabled", dict(kind=2, selection=1, flags=8, secondary=2)),
        ("kind2_even_selection", dict(kind=2, selection=2, flags=8)),
        ("kind3_enabled", dict(kind=3, selection=1)),
        ("kind3_zero_selection", dict(kind=3)),
        ("kind3_disabled", dict(kind=3, selection=1, secondary=8)),
        ("count_five_rejected", dict(secondary=4, count=5)),
        ("count_six_admitted", dict(secondary=4, count=6)),
        ("capability_rejected", dict(capability=False)),
        ("selection_rejected", dict(selection=4)),
        ("secondary_100_requires_40", dict(secondary=0x100)),
        ("secondary_100_with_40", dict(flags=0x40, secondary=0x100)),
        ("secondary_200_requires_20", dict(secondary=0x200)),
        ("secondary_200_with_20", dict(flags=0x20, secondary=0x200)),
        ("conflicting_20_40", dict(flags=0x60)),
        ("conflicting_40_80", dict(flags=0xC0)),
        ("secondary_pair_8000_rejected", dict(secondary=0x8000)),
        ("multiple_kind_requires_one", dict(kind_parameter=2, count_parameter=2)),
        ("distinct_owner_overrides", dict(override=True)),
        ("allocation_failure_releases_info", dict(override=True, allocation_failure=True)),
    ]
    for cid, arguments in specifications:
        yield resource_case(cid, **arguments)

    # Count=0 requests the leading-zero-count fallback. Seed the actual derived
    # number of entries; a sentinel immediately afterward rejects over-reads.
    for width, height in ((0, 0), (1, 0), (0, 1), (2, 0), (0, 2), (7, 0), (8, 0), (3, 4), (0x80000000, 0)):
        arguments = dict(initial=True, count_parameter=0, count=1, width=width, height=height)
        yield resource_case(f"derived_count_{width:x}_{height:x}", **arguments)
        last = max(1, width.bit_length(), height.bit_length()) - 1
        yield resource_case(
            f"derived_count_{width:x}_{height:x}_last_entry_zero", zero_entry=True, zero_entry_index=last, **arguments
        )
    yield resource_case("kind_one_zero_flags", kind=1, flags=0, initial=True)
    for flags in (0x90, 0x0C, 0x100):
        yield resource_case(f"rejected_flags_{flags:x}", flags=flags)
    yield resource_case("secondary_above_word", secondary=0x10000)
    yield resource_case("secondary_pair_8100_admitted", flags=0x40, secondary=0x8100)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for case in generated:
        (output / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
