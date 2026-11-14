#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD initialization-adapter cases. Usage: SCRIPT OUT_DIR.

The initialization callee is scripted. Cases compare the adapter's pointer and
DWORD argument forwarding and status, without opening ports or an allocator.
"""
import json
from pathlib import Path
import sys


ADDRESS = "0x0111b0e0"
INITIALIZE = "0x0111b600"
MASK64 = (1 << 64) - 1
NORMAL_STATUSES = [0, 0x25, 0x3C, 0x2B]


def case(cid, context="buf:owner+56", channel_count=2, buffer_samples=256,
         buffer_count=3, status=0, sample_rate="buf:rate+4",
         output_word="buf:output+4", unused=None, effects=False):
    """Keep callback writes owned; pointer-only cases never dereference inputs."""
    ignored = {"rsi": "0xfedcba9876543210", "rdx": "0x8000000000000001",
               "stack7": "0xabcdef1234567890"}
    assert not set(unused or {}) - set(ignored)
    ignored.update(unused or {})
    assert status in NORMAL_STATUSES
    owner_size = 0xA00 if effects else 256
    writes = []
    if effects:
        assert context == "buf:owner+56"
        writes = [{"arg": 5, "offset": 0, "bytes": "05000000"},
                  {"arg": 0, "offset": 0x6AC, "bytes": "67452301"},
                  {"arg": 0, "offset": 0x980, "bytes": "efcdab09"}]
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
        "args": {"rdi": context, "rsi": ignored["rsi"], "rdx": ignored["rdx"],
                 "rcx": sample_rate, "r8": hex(channel_count & MASK64),
                 "r9": output_word},
        "stack": [ignored["stack7"], hex(buffer_samples & MASK64),
                  hex(buffer_count & MASK64)],
        "buffers": {
            "owner": {"size": owner_size, "bytes": "a5" * owner_size},
            "rate": {"size": 32, "bytes": "b6" * 32},
            "output": {"size": 32, "bytes": "c7" * 32},
        },
        "memory": [{"addr": sample_rate, "bytes": "80bb0000"}]
                  if sample_rate.startswith("buf:") else [],
        "stubs": [{"address": INITIALIZE, "argc": 9, "argi": [0, 3, 5],
                   "argu32": [4, 7, 8], "ret": hex(status), "writes": writes}],
    }


def cases():
    """Exercise the logical interface without uninitialized unused stack slots."""
    contexts = ["0x0", "0x1", "0x37", "0x38", "0x39", "0x100000038",
                "0x8000000000000038", "0xffffffffffffffef",
                "buf:owner", "buf:owner+1", "buf:owner+55", "buf:owner+56",
                "buf:owner+57", "buf:owner+63", "buf:owner+127"]
    for index, context in enumerate(contexts):
        for status in NORMAL_STATUSES:
            yield case(f"context-{index}-status-{status}", context=context, status=status)

    values = [0, 1, 2, 3, 7, 8, 9, 255, 256, 257, 258, 0x10002, 0x10100,
              0x10003, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
              0x100000002, 0xABCDEF1200000100, 0xFFFFFFFF00000003]
    for field in ["channel_count", "buffer_samples", "buffer_count"]:
        for value in values:
            yield case(f"{field}-{value:016x}", **{field: value})

    for position in ["rsi", "rdx", "stack7"]:
        for index, value in enumerate(["0x0", "0x1", "0x100000000",
                                       "0x8000000000000000", "0xffffffffffffffff",
                                       "buf:owner+56"]):
            yield case(f"unused-{position}-{index}", unused={position: value})

    for position in ["sample_rate", "output_word"]:
        for index, pointer in enumerate(["0x0", "0x1", "0x100000004",
                                         "0x8000000000000004", "buf:rate+4",
                                         "buf:output+4", "buf:owner+64"]):
            yield case(f"pointer-{position}-{index}", **{position: pointer})

    yield case("owned-effects", effects=True)
    yield case("owned-rate-output-alias", sample_rate="buf:output+4", effects=True)
    yield case("owned-context-output-alias", output_word="buf:owner+1708", effects=True)
    yield case("owned-rate-context-alias", sample_rate="buf:owner+1708", effects=True)


def main():
    """Write deterministic uniquely named inputs outside the source checkout."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len(generated) == len({entry["id"] for entry in generated})
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
