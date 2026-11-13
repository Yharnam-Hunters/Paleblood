#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic instance step-rate cases, not recordings. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402

ADDRESS = "0x01474f90"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def case(cid, first=0x12345678, second=0x9ABCDEF0, available=4, result=0,
         cursor="packet", writes=()):
    """Keep an observing callback installed even when admission must skip it."""
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    buffer_address = SANDBOX + ["packet", "redirect", "self"].index(name) * 2 * PAGE
    limit = ({"pointer": f"{name}+{limit_offset}"} if 0 <= limit_offset <= 128
             else {"bytes": qword(buffer_address + limit_offset)})
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": "buf:self", "rsi": hex(first), "rdx": hex(second)},
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
    """Distinguish independent inputs, reservation paths and live publication."""
    values = [0, 1, 2, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFF,
              0x80000000, 0xFFFFFFFF, 0x100000000, 0xABCDEF1234567890]
    for value in values:
        yield case(f"first-{value:016x}", first=value)
        yield case(f"second-{value:016x}", second=value)
    for words in [-1, 0, 1, 2, 3, 4, 5, 0x7FFFFFFF, 0x80000000,
                  0xFFFFFFFF, 0x100000000, 0x100000003, 0x100000004,
                  (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-return-{result:016x}",
                       available=words, result=result)
    for result in [1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        yield case(f"callback-redirect-{result:016x}", available=3, result=result,
                   writes=[{"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
                           {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                           {"arg": 0, "offset": 32, "bytes": qword(0)}])
    for offset in [0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 41, 48]:
        yield case(f"overlap-{offset:02d}", cursor=f"self+{offset}")
    yield case("unaligned-packet", cursor="packet+1")
    yield case("callback-to-overlapping-cursor", available=0, result=2,
               writes=[{"arg": 0, "offset": 16, "pointer": "self+16", "size": 8}])
    for delta in [-3, -2, -1, 1, 2, 3, 13, 14, 15, 16, 17, 18, 19]:
        entry = case(f"byte-span-{delta}")
        entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(SANDBOX + delta)}
        yield entry
    entry = case("unaligned-prefix", available=3, result=2)
    entry["args"]["rdi"] = "buf:self+1"
    for preset in entry["memory"]:
        _, _, offset = preset["addr"].partition("+")
        preset["addr"] = f"buf:self+{int(offset or '0') + 1}"
    yield entry


def main():
    """Write deterministic synthetic fixtures to the requested directory."""
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
