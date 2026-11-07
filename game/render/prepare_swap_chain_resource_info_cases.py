#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: prepare_swap_chain_resource_info_cases.py OUT_DIR. Generate captures outside the repository.
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


def basic(cid, returns="i32", size=512):
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


ADDRESS = "0x02571d60"


def initial_cases():
    for stage in ("restricted", "descriptor_error", "allocation_error", "publish", "padded_publish"):
        c = basic(stage, size=80)
        c["buffers"].update(
            {
                "services": {"size": 320},
                "parameters": {"size": 64},
                "owner": {"size": 16},
                "vtable": {"size": 128},
                "allocation": {"size": 128, "bytes": "a5" * 128},
            }
        )
        c["args"].update(
            {"rsi": "buf:services", "rdx": "buf:parameters", "rcx": 0, "r8": "buf:owner", "r9": "buf:owner"}
        )
        c["stack"] = [0]
        c["memory"] = [
            {
                "addr": "buf:parameters",
                "bytes": "".join(
                    word(v)
                    for v in [
                        0x11223344,
                        0x55667788,
                        1,
                        2,
                        3,
                        16 if stage == "restricted" else 4,
                        5,
                        6,
                        0x80 if stage == "padded_publish" else 0,
                        7,
                        0,
                    ]
                ),
            },
            {"addr": "buf:owner", "pointer": "vtable"},
            {"addr": "buf:vtable+88", "guest": "0x014712b0"},
        ]
        if stage != "restricted":
            c["stubs"] = [
                {
                    "address": "0x02fa6af0",
                    "argc": 6,
                    "ret": 12 if stage == "descriptor_error" else 0,
                    "writes": [
                        {"arg": 0, "bytes": word(64) + word(8), "size": 8},
                        {"arg": 1, "bytes": "55" * 32, "size": 32},
                    ],
                }
            ]
            if stage != "descriptor_error":
                c["stubs"] += [
                    {"address": "0x01489910", "argc": 1, "ret": 2},
                    {"address": "0x014712b0", "argc": 3, "ret": 0 if stage == "allocation_error" else "buf:allocation"},
                ]
                if stage != "allocation_error":
                    c["stubs"] += [
                        {"address": "0x02abaef0", "argc": 0, "ret": 0x1234},
                        {"address": "0x014898f0", "argc": 1, "ret": 7},
                    ]
        yield c


def cases():
    yield from initial_cases()
    for path in (
        "predicate_skip",
        "span",
        "processor",
        "alternate_rejected",
        "offset_failure",
        "copy_failure",
        "alternate_success",
    ):
        for count in (0, 2):
            for levels in (0, 2):
                yield initial_data_case(path, count, levels)
    for capacity, alignment in ((32, 8), (64, 4), (96, 16)):
        c = next(c for c in initial_cases() if c["id"] == "publish")
        c["id"] = f"capacity_{capacity}_alignment_{alignment}"
        c["buffers"]["options"] = {"size": 32}
        c["stack"] = ["buf:options"]
        c["memory"].append({"addr": "buf:options", "bytes": "01000100" + word(capacity) + word(alignment)})
        # Admission fails after descriptor preparation, before levels/allocation.
        if capacity < 64 or alignment < 8:
            c["stubs"] = c["stubs"][:1]
        yield c

    # Distinct objects expose allocator selection, forwarding and release identity.
    for choose_first in (False, True):
        for failure in (False, True):
            c = initial_data_case("copy_failure", 1, 0) if failure else next(
                entry for entry in initial_cases() if entry["id"] == "publish"
            )
            c["id"] = f"distinct_owners_first{int(choose_first)}_copy_failure{int(failure)}"
            c["buffers"].update({"owner_a": {"size": 16}, "owner_b": {"size": 16}})
            c["args"].update({"r8": "buf:owner_a", "r9": "buf:owner_b"})
            c["memory"].extend([
                {"addr": "buf:owner_a", "pointer": "vtable"},
                {"addr": "buf:owner_b", "pointer": "vtable"},
                {"addr": "buf:parameters+40", "bytes": word(0x4000 if choose_first else 0)},
            ])
            yield c

    # Allocation padding or an explicit capacity must not enlarge the span copy.
    for padded in (False, True):
        c = initial_data_case("span", 1, 0)
        c["id"] = "span_required_bytes_with_padding" if padded else "span_required_bytes_with_capacity"
        if padded:
            c["memory"].append({"addr": "buf:parameters+32", "bytes": word(0x80)})
        else:
            c["memory"].append({"addr": "buf:options", "bytes": "01010100" + word(96) + word(8)})
        yield c

    yield from additional_cases()


    case = initial_data_case("alternate_success", 1, 0)
    case["id"] = "options_alternate_flag_published_through_output"
    case["stack"] = ["buf:self+29"]
    case["memory"] = [entry for entry in case["memory"] if entry["addr"] != "buf:options"]
    case["memory"].append({"addr": "buf:self+29", "bytes": "00000000"})
    # kind=3 at output+32 changes only the later alternate-path flag.
    # Keep an observing normal-path admission stub for the deliberately stale read.
    case["stubs"].append({"address": "0x02abadd0", "argc": 1, "ret": 0})
    yield case


def initial_data_case(path, count, levels):
    c = next(c for c in initial_cases() if c["id"] == "publish")
    c["id"] = f"{path}_count{count}_levels{levels}"
    c["buffers"].update(
        {
            "options": {"size": 32},
            "initial": {"size": 128},
            "input": {"size": 128, "bytes": "5b" * 128},
            "control": {"size": 16},
            "control_vtable": {"size": 64},
        }
    )
    c["args"]["rcx"] = "buf:initial"
    c["stack"] = ["buf:options"]
    c["memory"].extend(
        [
            {
                "addr": "buf:options",
                "bytes": bytes([0, path == "span", 1, path.startswith(("alternate", "offset", "copy"))]).hex(),
            },
            {"addr": "buf:parameters+12", "bytes": word(count)},
            {"addr": "buf:services+280", "pointer": "control"},
            {"addr": "buf:control", "pointer": "control_vtable"},
            {"addr": "buf:control_vtable+24", "guest": "0x01471270"},
            {"addr": "buf:control_vtable+40", "guest": "0x01471280"},
            {"addr": "buf:vtable+112", "guest": "0x01471290"},
        ]
    )
    for index in range(8):
        c["memory"].append({"addr": f"buf:initial+{index*16}", "pointer": f"input+{index*8}"})
    c["stubs"][1]["ret"] = levels

    def s(address, argc, ret=0, **extra):
        c["stubs"].append({"address": address, "argc": argc, "ret": ret, **extra})

    s("0x02abb4b0", 1, 0 if path == "predicate_skip" else 1)
    if path == "predicate_skip":
        return c
    if path == "span":
        s(
            "0x02a1a1b0",
            3,
            "buf:allocation",
            argmem=[{"arg": 1, "size": 64}],
            writes=[{"arg": 0, "bytes": "5b" * 64, "size": 64}],
        )
        return c
    if path == "processor":
        s("0x02abadd0", 1, 1)
        s("0x01471270", 2)
        for outer in range(levels + 1):
            for inner in range(count):
                s("0x02ab4230", 5, 0, argmem=[{"arg": 2, "size": 12}])
        s("0x02ab1930", 1)
        s("0x02ab18c0", 1)
        s("0x01471280", 1)
        return c
    s("0x02abad70", 1, 2 if path == "alternate_rejected" else 1)
    if path == "alternate_rejected":
        return c
    for outer in range(levels + 1):
        for inner in range(count):
            s(
                "0x014a2c40",
                5,
                0x12 if path == "offset_failure" else 0,
                writes=[{"arg": 0, "bytes": qword((outer * count + inner) * 8), "size": 8}],
            )
            if path == "offset_failure":
                s("0x02abb4a0", 1)
                return c
            s("0x014a1e80", 4, 0, writes=[{"arg": 0, "bytes": "6d" * 48, "size": 48}])
            s(
                "0x014a3410",
                3,
                0x13 if path == "copy_failure" else 0,
                argmem=[{"arg": 1, "size": 8}, {"arg": 2, "size": 48}],
                writes=[{"arg": 0, "bytes": "5b" * 8, "size": 8}],
            )
            if path == "copy_failure":
                s("0x01471290", 2)
                return c
    return c


def additional_cases():
    for remainder in range(1, 8):
        c = next(c for c in initial_cases() if c["id"] == "padded_publish")
        c["id"] = f"padded_size_mod8_{remainder}"
        c["stubs"][0]["writes"][0]["bytes"] = word(64 + remainder) + word(8)
        yield c

    for path in ("processor", "alternate_success"):
        for before, after in ((1, 2), (2, 1)):
            c = initial_data_case(path, 2, 2)
            c["id"] = f"{path}_callback_changes_count_{before}_to_{after}"
            alias_buffer = "self" if path == "processor" else "input"
            alias_offset = 64
            c["args"]["rdx"] = f"buf:{alias_buffer}+{alias_offset}"
            if alias_buffer == "self":
                c["buffers"]["self"] = {"size": 128, "bytes": "a5" * 128}
            c["buffers"].pop("parameters")
            for preset in c["memory"]:
                address = preset["addr"]
                if address.startswith("buf:parameters"):
                    offset = int(address.partition("+")[2] or "0", 0)
                    preset["addr"] = f"buf:{alias_buffer}+{alias_offset + offset}"
            c["memory"].append({"addr": f"buf:{alias_buffer}+{alias_offset + 12}", "bytes": word(before)})
            callback = "0x02ab4230" if path == "processor" else "0x014a3410"
            first = next(stub for stub in c["stubs"] if stub["address"] == callback)
            # The helper receives output or input storage that also contains the
            # parameter block. Only this first invocation changes the live bound.
            first.setdefault("writes", []).append(
                {"arg": 1, "offset": alias_offset + 12, "bytes": word(after), "size": 4}
            )
            yield c


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
