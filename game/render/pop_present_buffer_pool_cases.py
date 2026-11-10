#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: pop_present_buffer_pool_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ad3b80"


def cases():
    """Observe acquisition and sticky processed-marker status across the pool scan."""
    for count in (0, 1, 3):
        for marker in (0, 1, 2):
            for counter in (0, 0xFFFFFFFFFFFFFFFF):
                c = basic(f"blocks_{count}_marker_{marker}_counter_{counter:x}", returns="i32", size=64)
                c["buffers"].update({"pool": {"size": 128}, "output": {"size": 16, "bytes": "a5" * 16}})
                c["args"]["rsi"] = "buf:output"
                c["memory"] = [
                    {"addr": "buf:self+8", "bytes": qword(4) + qword(count)},
                    {"addr": "buf:self+24", "pointer": "pool"},
                ]
                for i in range(count):
                    c["memory"].append({"addr": f"buf:pool+{i*32+24}", "bytes": qword(marker)})
                c["stubs"] = [{"address": "0x0247eb80", "argc": 1, "ret": hex(counter)}]
                if count and marker == 0:
                    c["stubs"].append(
                        {
                            "address": "0x0247ebc0",
                            "argc": 3,
                            "ret": 0,
                            "writes": [{"arg": 0, "bytes": qword(1), "size": 8}],
                        }
                    )
                yield c

    # Keep the marker-derived status across failed compare/exchange calls. The
    # counter return -1 starts the scan at block zero.
    for name, markers, results in (
        ("released_then_failed_claim_one", (2, 0), (1,)),
        ("processed_then_busy_markers", (2, 1), ()),
        ("released_then_failed_claim_two", (2, 0), (2,)),
        ("failed_claim_two_without_released_marker", (0, 1), (2,)),
        ("failed_claim_one_without_released_marker", (0, 1), (1,)),
        ("failed_claim_then_success", (0, 0), (1, 0)),
    ):
        c = basic(name, returns="i32", size=64)
        c["buffers"].update({"pool": {"size": 128}, "output": {"size": 16, "bytes": "a5" * 16}})
        c["args"]["rsi"] = "buf:output"
        c["memory"] = [
            {"addr": "buf:self+8", "bytes": qword(4) + qword(len(markers))},
            {"addr": "buf:self+24", "pointer": "pool"},
            {"addr": "buf:self+32", "bytes": qword(0x123456789ABCDEF0)},
        ]
        for index, marker in enumerate(markers):
            c["memory"].append({"addr": f"buf:pool+{index * 32 + 24}", "bytes": qword(marker)})
        c["stubs"] = [{"address": "0x0247eb80", "argc": 1, "ret": hex(MASK64)}]
        c["stubs"] += [{"address": "0x0247ebc0", "argc": 3, "ret": result} for result in results]
        yield c

    c = basic("output_aliases_pool_counter", returns="i32", size=64)
    c["buffers"]["pool"] = {"size": 64}
    c["args"]["rsi"] = "buf:self+32"
    c["memory"] = [
        {"addr": "buf:self+8", "bytes": qword(4) + qword(1)},
        {"addr": "buf:self+24", "pointer": "pool"},
    ]
    c["stubs"] = [
        {"address": "0x0247eb80", "argc": 1, "ret": hex(MASK64)},
        {"address": "0x0247ebc0", "argc": 3, "ret": 0},
    ]
    yield c


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
