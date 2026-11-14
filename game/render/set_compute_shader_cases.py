#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic compute-shader wrapper cases, not recordings. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x01475840"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1
BUFFER_SIZES = {"packet": 512, "redirect": 512, "registers": 64, "self": 512}
BUFFER_ADDRESSES = {name: SANDBOX + index * 2 * PAGE
                    for index, name in enumerate(sorted(BUFFER_SIZES))}


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def pointer_address(pointer):
    name, _, offset = pointer.partition("+")
    return BUFFER_ADDRESSES[name] + int(offset or "0")


def case(cid, available=25, callback=0, library=0, cursor="packet",
         registers="registers", callback_writes=(), library_writes=(), prefix=0):
    """Observe both imports and the space callback, including unexpected calls."""
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    limit = ({"pointer": f"{name}+{limit_offset}"}
             if 0 <= limit_offset <= BUFFER_SIZES[name]
             else {"bytes": qword(BUFFER_ADDRESSES[name] + limit_offset)})
    imports = []
    for symbol in ("sceGnmSetCsShader", "sceGnmSetPsShader"):
        entry = {"name": symbol, "argc": 3, "argi": [0, 2], "argu32": [1],
                 "ret": hex(library & MASK64),
                 "writes": [{"arg": 0, "bytes": "39" * 100, "size": 100}] + list(library_writes)}
        if registers is not None:
            # This scripted observation checks forwarding; it does not model GNM.
            entry["argmem"] = [{"arg": 2, "size": 28}]
        imports.append(entry)
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{prefix}",
                 "rsi": "buf:" + registers if registers is not None else 0},
        "buffers": {name: {"size": size, "bytes": bytes([0xA5 + index]).hex() * size}
                    for index, (name, size) in enumerate(BUFFER_SIZES.items())},
        "memory": [
            {"addr": f"buf:self+{prefix}", "pointer": "packet"},
            {"addr": f"buf:self+{prefix + 8}", **limit},
            {"addr": f"buf:self+{prefix + 16}", "pointer": cursor},
            {"addr": f"buf:self+{prefix + 24}", "guest": CALLBACK},
            {"addr": f"buf:self+{prefix + 32}", "bytes": qword(0xFEDCBA9876543210)},
        ],
        "imports": imports,
        "stubs": [{"address": CALLBACK, "argc": 3, "argi": [0, 2], "argu32": [1],
                   "ret": hex(callback & MASK64), "writes": list(callback_writes)}],
    }


def cases():
    """Distinguish capacity, narrow result gates, forwarding and live publication."""
    for result in [0, 1, 2, 0x80, 0x100, 0x101, -1, 0x80000000,
                   0xFFFFFFFF, 0x100000000, 0x8000000000000000, 0xABCDEF0000000000]:
        yield case(f"library-{result & MASK64:016x}", library=result)
    for words in [-1, 0, 1, 23, 24, 25, 26, 0x7FFFFFFF, 0x80000000,
                  0xFFFFFFFF, 0x100000000, 0x100000018, 0x100000019, (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-return-{result:016x}",
                       available=words, callback=result)
    for result in [0, 1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        for library in [0, 1, 0x100000000]:
            yield case(f"callback-redirect-{result:016x}-library-{library:016x}",
                       available=24, callback=result, library=library,
                       callback_writes=[
                           {"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
                           {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                           {"arg": 0, "offset": 32, "bytes": qword(0)},
                       ])
    for result in [0, 1, 0x100, 0x80000000, 0x100000000]:
        yield case(f"library-updates-cursor-{result:016x}", library=result,
                   library_writes=[{"addr": "buf:self+16",
                                    "bytes": qword(pointer_address("redirect+7")), "size": 8}])
    for offset in [0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 41, 48]:
        for result in [0, 1]:
            # Scripted packet output may overwrite the prefix before the final add.
            yield case(f"overlap-{offset:02d}-library-{result}",
                       cursor=f"self+{offset}", library=result)
    for delta in [-3, -2, -1, 1, 2, 3, 95, 96, 97, 98, 99, 100, 101, 102, 103]:
        entry = case(f"byte-span-{delta}")
        entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(SANDBOX + delta)}
        yield entry
    for registers in ["registers+1", "packet", "packet+1", "self", "self+16", "redirect+3"]:
        yield case("registers-" + registers.replace("+", "-"), registers=registers)
    yield case("null-registers", registers=None, library=-1)
    yield case("unaligned-packet", cursor="packet+1")
    yield case("unaligned-prefix", available=24, callback=2, prefix=1)
    yield case("callback-to-overlapping-cursor", available=0, callback=2,
               callback_writes=[{"arg": 0, "offset": 16, "pointer": "self+16", "size": 8}])


def main():
    """Write deterministic fixtures outside the repository."""
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
