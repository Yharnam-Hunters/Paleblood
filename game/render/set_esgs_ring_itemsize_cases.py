#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic VGT_ESGS_RING_ITEMSIZE writer cases. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x01475a90"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1
BUFFER_NAMES = ["packet", "redirect", "self"]
BUFFER_BYTES = 128


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def case(cid, itemsize=0, available=3, result=0, cursor="packet", writes=(), prefix=0):
    """Observe the callback even when enough capacity must bypass it."""
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    address = SANDBOX + BUFFER_NAMES.index(name) * 2 * PAGE
    limit = ({"pointer": f"{name}+{limit_offset}"} if 0 <= limit_offset <= BUFFER_BYTES
             else {"bytes": qword(address + limit_offset)})
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{prefix}", "rsi": hex(itemsize & MASK64)},
        "buffers": {name: {"size": BUFFER_BYTES, "bytes": bytes([0xA5 + i]).hex() * BUFFER_BYTES}
                    for i, name in enumerate(BUFFER_NAMES)},
        "memory": [
            {"addr": f"buf:self+{prefix}", "pointer": "packet"},
            {"addr": f"buf:self+{prefix + 8}", **limit},
            {"addr": f"buf:self+{prefix + 16}", "pointer": cursor},
            {"addr": f"buf:self+{prefix + 24}", "guest": CALLBACK},
            {"addr": f"buf:self+{prefix + 32}", "bytes": qword(0xFEDCBA9876543210)},
        ],
        "stubs": [{"address": CALLBACK, "argc": 3, "argi": [0, 2], "argu32": [1],
                   "ret": hex(result & MASK64), "writes": list(writes)}],
    }


def cases():
    """Distinguish the mask, narrow admission and captured publication."""
    for bit in range(32):
        yield case(f"itemsize-bit-{bit:02d}", 1 << bit)
    boundaries = [0, 1, 2, 3, 0x7F, 0x80, 0xFF, 0x100, 0x3FFF, 0x4000,
                  0x7FFE, 0x7FFF, 0x8000, 0x8001, 0xFFFE, 0xFFFF,
                  0x10000, 0x10001, 0x7FFFFFFF, 0xFFFFFFFF]
    for value in boundaries:
        yield case(f"itemsize-{value:08x}", value)
    for high in [0x100000000, 0x8000000000000000, 0xABCDEF1200000000]:
        for low in [0, 1, 0x4000, 0x7FFF]:
            yield case(f"upper-{high:016x}-{low:04x}", high | low)
    for words in [-1, 0, 1, 2, 3, 4, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                  0x100000000, 0x100000002, 0x100000003, (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-{result:016x}", 0x4123,
                       available=words, result=result)
    for result in [0, 1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        yield case(f"redirect-{result:016x}", 0x4567, available=0, result=result,
                   writes=[
                       {"arg": 0, "offset": 0, "pointer": "redirect", "size": 8},
                       {"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
                       {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                       {"arg": 0, "offset": 32, "bytes": qword(0)},
                   ])
    for offset in [0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 41, 48]:
        yield case(f"overlap-{offset:02d}", 0x4A35, cursor=f"self+{offset}")
    for delta in [-3, -2, -1, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15]:
        entry = case(f"byte-span-{delta}", 0x4567)
        entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(SANDBOX + delta)}
        yield entry
    yield case("unaligned-packet", 0x4A35, cursor="packet+1")
    yield case("unaligned-prefix", 0x4A35, available=0, result=2, prefix=1)
    yield case("callback-overlap", 0x4A35, available=0, result=2,
               writes=[{"arg": 0, "offset": 16, "pointer": "self+16", "size": 8}])


def main():
    """Write deterministic fixtures with unique IDs outside the source tree."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len({entry["id"] for entry in generated}) == len(generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
