#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: present_source_prepare_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x02ad1eb0"


def cases():
    for count in (0, 1, 3):
        for fail in (-1, 0, 1):
            c = basic(f"count_{count}_failure_{fail}", returns="void", size=512)
            c["buffers"].update({"context": {"size": 64}, "source": {"size": 128}})
            c["args"].update({"rsi": "buf:context", "rdx": "buf:source"})
            c["memory"] = [{"addr": "buf:source+68", "bytes": "341278560500" + bytes([count, 7]).hex()}]
            c["stubs"] = [{"address": "0x02ad1f80", "argc": 3, "ret": 0}]
            n = count
            if count == 0:
                n = 2
                c["stubs"].append({"address": "0x02565130", "argc": 3, "ret": 2})
            for i in range(n):
                result = 12 if i == fail else 0
                c["stubs"].append(
                    {
                        "address": "0x0256e1d0",
                        "argc": 6,
                        "ret": result,
                        "writes": [{"arg": 0, "bytes": "6b" * 48, "size": 48}],
                    }
                )
                if result:
                    break
                c["stubs"].append({"address": "0x02ab5a70", "argc": 6, "ret": 0, "argmem": [{"arg": 1, "size": 48}]})
            yield c

    # Builder and consumer writes reach payload+0x30 through their real payload
    # arguments. Each later call must observe the new parameter and selector.
    for selector in (0, 1, 0xFF):
        c = basic(f"builders_change_payload_selector_{selector:02x}", returns="void", size=512)
        c["buffers"].update({"context": {"size": 64}, "source": {"size": 128}})
        c["args"].update({"rsi": "buf:context", "rdx": "buf:source"})
        c["memory"] = [{"addr": "buf:source+68", "bytes": "341278565713" + bytes([3, selector]).hex()}]
        c["stubs"] = [{"address": "0x02ad1f80", "argc": 3, "ret": 0}]
        for index in range(3):
            c["stubs"].append(
                {
                    "address": "0x0256e1d0",
                    "argc": 6,
                    "ret": 0,
                    "writes": [
                        {"arg": 0, "bytes": f"{0x71 + index:02x}" * 48, "size": 48},
                        {"arg": 1, "offset": 0x30, "bytes": struct.pack("<H", 0x2000 + index).hex(), "size": 2},
                        {"arg": 1, "offset": 0x33, "bytes": f"{(selector + index + 1) & 0xff:02x}", "size": 1},
                    ],
                }
            )
            c["stubs"].append(
                {
                    "address": "0x02ab5a70",
                    "argc": 6,
                    "ret": 0,
                    "argmem": [{"arg": 1, "size": 48}],
                    "writes": [
                        {"arg": 2, "offset": 0x30, "bytes": struct.pack("<H", 0x3000 + index).hex(), "size": 2},
                        {"arg": 2, "offset": 0x33, "bytes": f"{(selector + index + 2) & 0xff:02x}", "size": 1},
                    ],
                }
            )
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
