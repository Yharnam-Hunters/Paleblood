#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic IA_MULTI_VGT_PARAM writer cases. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x01475960"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def case(cid, group_size=0, partial_vs=0, available=3, result=0, cursor="packet", writes=()):
    """Install an observing callback even when sufficient space must bypass it."""
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    address = SANDBOX + ["packet", "redirect", "self"].index(name) * 2 * PAGE
    limit = ({"pointer": f"{name}+{limit_offset}"} if 0 <= limit_offset <= 128
             else {"bytes": qword(address + limit_offset)})
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": "buf:self", "rsi": hex(group_size), "rdx": hex(partial_vs)},
        "buffers": {
            "packet": {"size": 128, "bytes": "a5" * 128},
            "redirect": {"size": 128, "bytes": "b6" * 128},
            "self": {"size": 128, "bytes": "c7" * 128},
        },
        "memory": [
            {"addr": "buf:self", "pointer": "packet"},
            {"addr": "buf:self+8", **limit},
            {"addr": "buf:self+16", "pointer": cursor},
            {"addr": "buf:self+24", "guest": CALLBACK},
            {"addr": "buf:self+32", "bytes": qword(0xFEDCBA9876543210)},
        ],
        "stubs": [{"address": CALLBACK, "argc": 3, "argi": [0, 2],
                   "argu32": [1], "ret": hex(result), "writes": list(writes)}],
    }


def cases():
    """Isolate the low size byte, low flag bit, admission and cursor publication."""
    for bit in range(32):
        yield case(f"group-bit-{bit:02d}", group_size=1 << bit)
        yield case(f"partial-vs-bit-{bit:02d}", partial_vs=1 << bit)
    group_sizes = [0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x101,
                   0x1FF, 0xFFFF, 0x10000, 0xFFFFFFFF]
    wave_flags = [0, 1, 2, 3, 0x80, 0x81, 0xFF, 0x100,
                  0x101, 0x10000, 0x10001, 0xFFFFFFFF]
    for group_size in group_sizes:
        for partial_vs in wave_flags:
            yield case(f"fields-{group_size:08x}-{partial_vs:08x}", group_size, partial_vs)
    for high in [0x100000000, 0x8000000000000000, 0xABCDEF1200000000]:
        for low in [0, 1, 0x80, 0xFF]:
            yield case(f"group-upper-{high:016x}-{low:03x}", high | low, 0x615)
            yield case(f"partial-vs-upper-{high:016x}-{low:03x}", 0x701, high | low)
    for words in [-1, 0, 1, 2, 3, 4, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                  0x100000000, 0x100000002, 0x100000003, (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-{result:016x}", 0x701, 0x615,
                       available=words, result=result)
    for result in [0, 1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        writes = [
            {"arg": 0, "offset": 0, "pointer": "redirect", "size": 8},
            {"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
            {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
            {"arg": 0, "offset": 32, "bytes": qword(0)},
        ]
        yield case(f"redirect-{result:016x}", 0x701, 0x615,
                   available=0, result=result, writes=writes)
    for offset in [0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 41, 48]:
        yield case(f"overlap-{offset:02d}", 0x701, 0x615, cursor=f"self+{offset}")
    yield case("unaligned-packet", 0x701, 0x615, cursor="packet+1")
    yield case("callback-overlap", 0x701, 0x615, available=0, result=2,
               writes=[{"arg": 0, "offset": 16, "pointer": "self+16", "size": 8}])
    for delta in [-3, -2, -1, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15]:
        entry = case(f"byte-span-{delta}", 0x701, 0x615)
        entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(SANDBOX + delta)}
        yield entry
    entry = case("unaligned-prefix", 0x701, 0x615, available=0, result=2)
    entry["args"]["rdi"] = "buf:self+1"
    for preset in entry["memory"]:
        _, _, offset = preset["addr"].partition("+")
        preset["addr"] = f"buf:self+{int(offset or '0') + 1}"
    yield entry


def main():
    """Write deterministic fixtures with unique IDs outside the source tree."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    generated = list(cases())
    assert len({entry["id"] for entry in generated}) == len(generated)
    out.mkdir(parents=True, exist_ok=True)
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
