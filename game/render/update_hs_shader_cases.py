#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic hull-shader update cases, not recordings. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x01475640"
CALLBACK = "0x014712b0"
MASK64 = (1 << 64) - 1
BUFFER_SIZES = {"configuration": 64, "packet": 512, "redirect": 512, "registers": 64, "self": 512}
BUFFER_ADDRESSES = {name: SANDBOX + index * 2 * PAGE
                    for index, name in enumerate(sorted(BUFFER_SIZES))}


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def pointer_address(pointer):
    name, _, offset = pointer.partition("+")
    return BUFFER_ADDRESSES[name] + int(offset or "0")


def case(cid, available=30, callback=0, library=0, cursor="packet",
         registers="registers", callback_writes=(), library_writes=(), prefix=0,
         configuration="configuration", scalar=0, initialize_configuration=True):
    """Observe both imports and the space callback, including unexpected calls."""
    name, _, offset = cursor.partition("+")
    limit_offset = int(offset or "0") + available * 4
    limit = ({"pointer": f"{name}+{limit_offset}"}
             if 0 <= limit_offset <= BUFFER_SIZES[name]
             else {"bytes": qword(BUFFER_ADDRESSES[name] + limit_offset)})
    imports = []
    for symbol in ("sceGnmUpdateHsShader", "sceGnmSetVsShader"):
        entry = {"name": symbol, "argc": 4, "argi": [0, 2], "argu32": [1, 3],
                 "ret": hex(library & MASK64),
                 "writes": [{"arg": 0, "bytes": "39" * 120, "size": 120}] + list(library_writes)}
        if registers is not None:
            # This scripted observation checks forwarding; it does not model GNM.
            entry["argmem"] = [{"arg": 2, "size": 28}]
        imports.append(entry)
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{prefix}",
                 "rsi": "buf:" + registers if registers is not None else 0,
                 "rdx": "buf:" + configuration if configuration is not None else 0},
        "buffers": {name: {"size": size, "bytes": bytes([0xA5 + index]).hex() * size}
                    for index, (name, size) in enumerate(BUFFER_SIZES.items())},
        "memory": [
            {"addr": f"buf:self+{prefix}", "pointer": "packet"},
            {"addr": f"buf:self+{prefix + 8}", **limit},
            {"addr": f"buf:self+{prefix + 16}", "pointer": cursor},
            {"addr": f"buf:self+{prefix + 24}", "guest": CALLBACK},
            {"addr": f"buf:self+{prefix + 32}", "pointer": "configuration"},
        ] + ([{"addr": "buf:" + configuration, "bytes": qword(scalar)}]
             if configuration is not None and initialize_configuration else []),
        "imports": imports,
        "stubs": [{"address": CALLBACK, "argc": 3, "argi": [0, 2], "argu32": [1],
                   "ret": hex(callback & MASK64), "writes": list(callback_writes)}],
    }


def cases():
    """Distinguish capacity, narrow result gates, forwarding and live publication."""
    for result in [0, 1, 2, 0x80, 0x100, 0x101, -1, 0x80000000,
                   0xFFFFFFFF, 0x100000000, 0x8000000000000000, 0xABCDEF0000000000]:
        yield case(f"library-{result & MASK64:016x}", library=result)
    for words in [-1, 0, 1, 28, 29, 30, 31, 0x7FFFFFFF, 0x80000000,
                  0xFFFFFFFF, 0x100000000, 0x10000001D, 0x10000001E, (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-return-{result:016x}",
                       available=words, callback=result)
    for result in [0, 1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        for library in [0, 1, 0x100000000]:
            yield case(f"callback-redirect-{result:016x}-library-{library:016x}",
                       available=29, callback=result, library=library,
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
    for delta in [-3, -2, -1, 1, 2, 3, 115, 116, 117, 118, 119, 120, 121, 122, 123]:
        entry = case(f"byte-span-{delta}")
        entry["memory"][1] = {"addr": "buf:self+8", "bytes": qword(BUFFER_ADDRESSES["packet"] + delta)}
        yield entry
    for registers in ["registers+1", "packet", "packet+1", "self", "self+16", "redirect+3"]:
        yield case("registers-" + registers.replace("+", "-"), registers=registers)
    yield case("null-registers", registers=None, library=-1)
    yield case("unaligned-packet", cursor="packet+1")
    yield case("unaligned-prefix", available=29, callback=2, prefix=1)
    yield case("callback-to-overlapping-cursor", available=0, callback=2,
               callback_writes=[{"arg": 0, "offset": 16, "pointer": "self+16", "size": 8}])
    scalars = [0, 1, 2, 3, 0x7F, 0x80, 0xFE, 0xFF, 0x100, 0x101,
                 0x1FF, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
                 0x80000000, 0xFFFFFFFF, 0x100000000, 0xABCDEF1234567801]
    scalars += [1 << bit for bit in range(2, 32)]
    for scalar in dict.fromkeys(scalars):
        yield case(f"configuration-{scalar:016x}", scalar=scalar)
    for scalar in [0, 1, 2, 0x80, 0x100, 0x8000, 0x10000,
                     0x80000080, 0xFFFFFFFF, 0xABCDEF1234567801]:
        yield case(f"configuration-callback-{scalar:016x}", available=29,
                   callback=2, scalar=scalar, callback_writes=[
                       {"arg": 0, "offset": 16, "pointer": "redirect+1", "size": 8},
                       {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                       {"arg": 0, "offset": 32, "bytes": qword(0)},
                   ])
    for pointer in ["configuration+1", "configuration+3", "packet+4", "redirect+7"]:
        yield case("configuration-pointer-" + pointer.replace("+", "-"),
                   configuration=pointer, scalar=0xD4C3B2A187654321)
    for result in [1, 2, 0x80, 0x101, 0xABCDEF0000000002]:
        for value in [2, 0x80, 0x10000, 0x80010080, 0xFFFFFFFF]:
            yield case(f"callback-writes-configuration-{result:016x}-{value:08x}",
                       available=29, callback=result, scalar=0x11223344,
                       callback_writes=[{"arg": 2, "bytes": qword(value)}])
    for result in [0, 0x100, 0x8000000000000000]:
        yield case(f"null-configuration-declined-{result:016x}", available=29,
                   callback=result, configuration=None)
        entry = case(f"unmapped-configuration-declined-{result:016x}", available=29,
                     callback=result, configuration=None)
        entry["args"]["rdx"] = "0x123456789abcdef0"
        yield entry
    for offset in [0, 8, 16, 20, 24, 32, 40, 41]:
        yield case(f"configuration-prefix-alias-{offset:02d}",
                   configuration=f"self+{offset}", initialize_configuration=False)
    yield case("callback-writes-configuration-prefix-alias", available=29, callback=2,
               configuration="self+40", initialize_configuration=False,
               callback_writes=[{"arg": 0, "offset": 40, "bytes": qword(0x80010080)}])
    yield case("callback-preserves-configuration-pointer", available=29, callback=2,
               scalar=0x80010080, callback_writes=[
                   {"arg": 0, "offset": 32, "pointer": "redirect", "size": 8}])
    yield case("configuration-output-alias", configuration="packet", scalar=0x80010080)
    for value in [0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFF87, 0xFFFFFFFFFFFFFF88,
                  0xFFFFFFFFFFFFFF89, 0x8000000000000000]:
        for result in [0, 1]:
            yield case(f"cursor-wrap-{value:016x}-library-{result}", library=result,
                       library_writes=[{"addr": "buf:self+16", "bytes": qword(value), "size": 8}])


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
