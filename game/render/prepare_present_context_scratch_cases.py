#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: prepare_present_context_scratch_cases.py OUT_DIR. Generate captures outside the repository.
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


def basic(cid, returns="void", size=512):
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


ADDRESS = "0x02ad3070"


def base_cases():
    for bank in (0, 1):
        for typ in (0, 1, 2, 7, 8, 9):
            yield scratch_case(f"ordinary_bank{bank}_type{typ}", bank, typ, False)
        for typ in (0, 1, 2, 6, 7):
            yield scratch_case(f"last_bank{bank}_type{typ}", bank, typ, True)
    for sentinel in (0, 0xFFFFFFFFFFFFFFFF):
        c = scratch_case(f"last_sentinel_{sentinel:016x}", 0, 0, True)
        c["memory"][-1] = {"addr": "buf:self+160", "bytes": qword(sentinel)}
        c["stubs"] = []
        yield c


def scratch_case(cid, bank, typ, last):
    c = basic(cid, size=512)
    c["buffers"]["self"]["bytes"] = "00" * 512
    c["buffers"].update(
        {
            "context": {"size": 64},
            "snapshot": {"size": 72},
            "record": {"size": 96, "bytes": "72" * 96},
            "resource": {"size": 64},
        }
    )
    c["args"].update({"rsi": "buf:context", "rdx": "buf:snapshot"})
    c["memory"] = [
        memory(0x188, bank * 3),
        {"addr": "buf:record+8", "pointer": "resource"},
        {"addr": "buf:resource+59", "bytes": "8b"},
        {"addr": "buf:record+64", "bytes": word(typ)},
        {"addr": "buf:record+72", "bytes": word(typ) + word(0x87654321)},
        {"addr": "buf:record+84", "bytes": word(0x12345678) + word(0x9ABCDEF0) + word(0)},
        {"addr": f"buf:self+{0x60+bank*0x48+(0x40 if last else 0)}", "pointer": "record"},
    ]
    if 1 <= typ <= 6 if last else 1 <= typ <= 8:
        if not last:
            c["stubs"].append({"address": "0x02ad1f80", "argc": 2, "ret": 0})
        c["stubs"].append({"address": "0x02ab6810" if last else "0x02ab5a70", "argc": 6, "ret": "0x89abcdef01234567"})
    return c


def cases():
    yield from base_cases()
    for flags in (1, 2):
        for last in (False, True):
            bank = ((flags >> 1) & 1) if last else (flags & 1)
            c = scratch_case(f"independent_flag_{flags}_last_{int(last)}", bank, 2, last)
            c["memory"][0]["bytes"] = word(flags)
            yield c
    c = scratch_case("last_nonzero_word_5c", 0, 2, True)
    c["memory"][5]["bytes"] = word(0x12345678) + word(0x9ABCDEF0) + word(1)
    c["stubs"] = []
    yield c
    c = scratch_case("ordinary_parameters_captured_before_refresh", 0, 2, False)
    # Embed this record in writable state so refresh can replace its parameters.
    # The submission must still receive the pair loaded before that callback.
    c["args"]["rsi"] = "buf:self"
    for preset in c["memory"]:
        if preset["addr"].startswith("buf:record+"):
            offset = int(preset["addr"].split("+")[1], 0)
            preset["addr"] = f"buf:self+{0x1A0 + offset}"
        if preset.get("pointer") == "record":
            preset["pointer"] = "self+0x1a0"
    c["stubs"][0]["writes"] = [{"arg": 0, "offset": 0x1E8, "bytes": word(0x10203040) + word(0x50607080), "size": 8}]
    yield c

    for flags in (0x80, 0x08, 0x01):
        c = scratch_case(f"ordinary_resource_flag_{flags:02x}", 0, 2, False)
        c["memory"][2]["bytes"] = f"{flags:02x}"
        yield c
    c = scratch_case("refresh_switches_between_two_live_banks", 0, 2, False)
    c["buffers"]["old_record"] = {"size": 96, "bytes": "63" * 96}
    c["buffers"]["new_record"] = {"size": 96, "bytes": "94" * 96}
    c["memory"] += [
        {"addr": "buf:self+0x68", "pointer": "old_record"},
        {"addr": "buf:self+0xb0", "pointer": "new_record"},
    ]
    for name, first in (("old_record", 0x10203040), ("new_record", 0x50607080)):
        c["memory"] += [
            {"addr": f"buf:{name}+8", "pointer": "resource"},
            {"addr": f"buf:{name}+64", "bytes": word(2)},
            {"addr": f"buf:{name}+72", "bytes": word(first) + word(0x87654321)},
        ]
    c["stubs"][0]["writes"] = [{"arg": 0, "offset": 0x188, "bytes": word(1), "size": 4}]
    c["stubs"] += [
        {"address": "0x02ad1f80", "argc": 2, "ret": 0},
        {"address": "0x02ab5a70", "argc": 6, "ret": 0},
    ]
    yield c


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    # Capture defined low bits of integer parameters, excluding ABI padding.
    for entry in generated:
        for callback in entry.get("stubs", []):
            address = callback["address"]
            if address in ("0x02ab5a70", "0x02ab6810"):
                callback.update(argi=[0, 1, 2], argu32=[3, 4, 5])
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
