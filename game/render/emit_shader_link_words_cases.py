#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: emit_shader_link_words_cases.py OUT_DIR. Generate captures outside the repository.
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


ADDRESS = "0x01475000"


def cases():
    for count in (0, 1, 2, 4, 16, 33):
        for available, result in ((80, 0), (0, 0), (0, 1)):
            c = basic(f"count_{count}_space_{available}_ret_{result}", returns="void", size=64)
            c["buffers"].update(
                {
                    "packet": {"size": 512, "bytes": "6b" * 512},
                    "source": {"size": 256, "bytes": bytes(range(256)).hex()},
                }
            )
            c["args"].update({"rsi": "buf:source", "rdx": count})
            c["memory"] = [
                {"addr": "buf:self+8", "pointer": f"packet+{available*4}"},
                {"addr": "buf:self+16", "pointer": "packet"},
                {"addr": "buf:self+24", "guest": "0x014712b0"},
                {"addr": "buf:self+32", "bytes": qword(7)},
            ]
            if available < count + 2:
                c["stubs"] = [{"address": "0x014712b0", "argc": 3, "ret": result}]
            yield c

    for count in (31, 32, 33):
        c = basic(f"boundary_count_{count}_ample_space", returns="void", size=64)
        c["buffers"].update(
            {"packet": {"size": 512, "bytes": "6b" * 512}, "source": {"size": 256, "bytes": bytes(range(256)).hex()}}
        )
        c["args"].update({"rsi": "buf:source", "rdx": count})
        c["memory"] = [
            {"addr": "buf:self+8", "pointer": "packet+512"},
            {"addr": "buf:self+16", "pointer": "packet"},
            {"addr": "buf:self+24", "guest": "0x014712b0"},
            {"addr": "buf:self+32", "bytes": qword(7)},
        ]
        yield c

    c = basic("source_one_word_before_payload", returns="void", size=64)
    c["buffers"]["packet"] = {"size": 512, "bytes": bytes(range(256)).hex() * 2}
    c["args"].update({"rsi": "buf:packet+4", "rdx": 4})
    c["memory"] = [{"addr": "buf:self+8", "pointer": "packet+512"}, {"addr": "buf:self+16", "pointer": "packet"}]
    yield c
    c = basic("payload_overwrites_live_cursor", returns="void", size=128)
    c["buffers"]["source"] = {"size": 64, "bytes": "78563412" * 16}
    c["args"].update({"rsi": "buf:source", "rdx": 4})
    c["memory"] = [{"addr": "buf:self+8", "pointer": "self+128"}, {"addr": "buf:self+16", "pointer": "self+8"}]
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
