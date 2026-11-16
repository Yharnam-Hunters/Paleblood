#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic register-space WAIT_REG_MEM cases. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x01475ec0"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1
BUFFER_NAMES = ["packet", "redirect", "self"]
BUFFER_BYTES = 128


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def case(cid, register=0x1234ABCD, mask=0xA5963C5A, comparison=5,
         reference=0x89ABCDEF, available=7, result=0, cursor="packet",
         writes=(), prefix=0):
    """Observe callback arguments even in cases that must bypass the callback."""
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    address = SANDBOX + BUFFER_NAMES.index(name) * 2 * PAGE
    limit = ({"pointer": f"{name}+{limit_offset}"} if 0 <= limit_offset <= BUFFER_BYTES
             else {"bytes": qword(address + limit_offset)})
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{prefix}", "rsi": hex(register & MASK64),
                 "rdx": hex(mask & MASK64), "rcx": hex(comparison & MASK64),
                 "r8": hex(reference & MASK64)},
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
    """Distinguish every input bit, admission width, and live publication."""
    yield case("distinct-inputs")
    for field in ["register", "mask", "comparison", "reference"]:
        for bit in range(64):
            yield case(f"{field}-bit-{bit:02d}", **{field: 1 << bit})
        for value in [0, 1, 2, 3, 4, 6, 7, 8, 0xFF, 0x100, 0x7FFF,
                      0x8000, 0xFFFF, 0x10000, 0xFFFFFFFF, MASK64]:
            yield case(f"{field}-value-{value:016x}", **{field: value})
    # The shift is unsigned and precedes truncation to a DWORD word count.
    for words in [-1, 0, 1, 5, 6, 7, 8, 0x7FFFFFFF, 0x80000000,
                  0xFFFFFFFF, 0x100000000, 0x100000006, 0x100000007,
                  (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-{result:016x}", available=words, result=result)
    for result in [0, 1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        yield case(f"redirect-{result:016x}", available=0, result=result,
                   writes=[
                       {"arg": 0, "offset": 0, "pointer": "redirect", "size": 8},
                       {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                       {"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
                       {"arg": 0, "offset": 32, "bytes": qword(0)},
                   ])
    # Only the final cursor value is observed; no subsequent dereference is needed
    # when the packet overwrites that field with a non-pointer bit pattern.
    for offset in range(41):
        yield case(f"overlap-{offset:02d}", cursor=f"self+{offset}")
    for offset in [0, 1, 8, 12, 16, 20]:
        yield case(f"callback-overlap-{offset:02d}", available=0, result=2,
                   writes=[{"arg": 0, "offset": 16, "pointer": f"self+{offset}", "size": 8}])
    for delta in [-3, -2, -1, 0, 1, 2, 3, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]:
        entry = case(f"byte-span-{delta}")
        entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(SANDBOX + delta)}
        yield entry
    for prefix in [1, 2, 3]:
        yield case(f"unaligned-prefix-{prefix}", prefix=prefix, available=0, result=2)
    for offset in [1, 2, 3]:
        yield case(f"unaligned-packet-{offset}", cursor=f"packet+{offset}")
    yield case("publication-add-wraps", mask=0xFFFFFFFF, reference=0xFFFFFFFF,
               cursor="self")


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
