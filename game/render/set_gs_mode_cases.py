#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic GS-mode and raw-zero streamout-reset cases. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x014756b0"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def case(cid, mode=1, cut=0, available=None, result=0, cursor="packet", writes=()):
    """Install an observing space callback even when capacity should bypass it."""
    requested = 6 if mode & 0xFFFFFFFF == 0 else 3
    if available is None:
        available = requested
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    address = SANDBOX + ["packet", "redirect", "self"].index(name) * 2 * PAGE
    limit = ({"pointer": f"{name}+{limit_offset}"} if 0 <= limit_offset <= 128
             else {"bytes": qword(address + limit_offset)})
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": "buf:self", "rsi": hex(mode), "rdx": hex(cut)},
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
    """Vary independent packed bits, raw-zero branching, admission and overlap."""
    for bit in range(32):
        yield case(f"mode-bit-{bit:02d}", mode=1 << bit)
        yield case(f"cut-bit-{bit:02d}", mode=0xFFFFFFFF, cut=1 << bit)
        for mode in [0, 1]:
            yield case(f"isolated-cut-{mode}-{bit:02d}", mode, 1 << bit)
    for mode in [0, 1, 0x10, 0x20, 0x30, 0x40, 0x180003, 0x600003, 0xFFFFFFFF]:
        for cut in [0, 1, 2, 3]:
            yield case(f"fields-{mode:08x}-{cut}", mode, cut)
    for high in [0x100000000, 0x8000000000000000, 0xABCDEF1200000000]:
        for low in [0, 1, 0x10, 0xFFFFFFFF]:
            yield case(f"mode-upper-{high:016x}-{low:08x}", high | low, 2)
        for low in [0, 1, 2, 3]:
            yield case(f"cut-upper-{high:016x}-{low}", 0x30, high | low)
    for mode in [0, 0x10]:
        for words in [-1, 0, 1, 2, 3, 4, 5, 6, 7, 0x7FFFFFFF, 0x80000000,
                      0xFFFFFFFF, 0x100000000, 0x100000003, 0x100000006, (1 << 62) - 1]:
            for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
                yield case(f"capacity-{mode:02x}-{words:016x}-{result:016x}",
                           mode, 3, available=words, result=result)
        for result in [1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
            writes = [
                {"arg": 0, "offset": 0, "pointer": "redirect", "size": 8},
                {"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
                {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                {"arg": 0, "offset": 32, "bytes": qword(0)},
            ]
            yield case(f"redirect-{mode:02x}-{result:016x}", mode, 2,
                       available=0, result=result, writes=writes)
        for offset in [0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 41, 48]:
            yield case(f"overlap-{mode:02x}-{offset:02d}", mode, 2, cursor=f"self+{offset}")
        yield case(f"unaligned-packet-{mode:02x}", mode, 3, cursor="packet+1")
        yield case(f"callback-overlap-{mode:02x}", mode, 1, available=0, result=2,
                   writes=[{"arg": 0, "offset": 16, "pointer": "self+16", "size": 8}])
        for delta in [-3, -2, -1, 1, 2, 3, 9, 10, 11, 12, 13, 14, 15, 21, 22, 23, 24, 25, 26, 27]:
            entry = case(f"byte-span-{mode:02x}-{delta}", mode, 1)
            entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(SANDBOX + delta)}
            yield entry
        entry = case(f"unaligned-prefix-{mode:02x}", mode, 2, available=0, result=2)
        entry["args"]["rdi"] = "buf:self+1"
        for preset in entry["memory"]:
            _, _, offset = preset["addr"].partition("+")
            preset["addr"] = f"buf:self+{int(offset or '0') + 1}"
        yield entry
    # A second reservation is wrong. Observe it with a second valid script,
    # rather than mistaking an unscripted-call error for a distinguishing result.
    entry = case("single-reservation", 0, 3, available=5, result=2)
    entry["stubs"].append({"address": CALLBACK, "argc": 3, "argi": [0, 2],
                            "argu32": [1], "ret": "0x0"})
    yield entry


def main():
    """Write deterministic, uniquely named synthetic fixtures outside the tree."""
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
