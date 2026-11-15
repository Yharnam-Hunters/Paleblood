#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD output-label cases. Usage: SCRIPT OUT_DIR.

Nested cases execute the original counted-copy helper. Scripted cases observe the
caller's arguments and final store with explicitly synthetic helper effects.
"""
import json
from pathlib import Path
import sys


ADDRESS = "0x0111b070"
COPY_HELPER = "0x011c4840"
UNCOUNTED_COPY_HELPER = "0x011c4810"
MASK64 = (1 << 64) - 1


def case(cid, count=23, offset=2, size=None, destination=None, scripted=False,
         writes=(), helper_return="buf:output", incoming=None, stack=()):
    """Keep every reached destination byte owned; never modify the fixed label."""
    low = count & 0xFFFFFFFF
    signed = low if low < 0x80000000 else low - (1 << 32)
    size = max(128, offset + max(0, signed) + 8) if size is None else size
    assert 0 <= offset < size
    args = {"rdi": "0xfedcba9876543210", "rsi": "0x8000000000000001",
            "rdx": destination or f"buf:output+{offset}", "rcx": hex(count & MASK64),
            "r8": "0xabcdef1234567890", "r9": "0xffffffffffffffff"}
    assert not set(incoming or {}) & {"rdx", "rcx"}
    args.update(incoming or {})
    entry = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i64",
             "args": args, "buffers": {"output": {"size": size, "bytes": "a5" * size}},
             "memory": [], "stubs": []}
    if stack:
        entry["stack"] = list(stack)
    if scripted:
        entry["stubs"] = [{"address": COPY_HELPER, "argc": 3, "argi": [0, 1],
                           "argu32": [2], "ret": helper_return, "writes": list(writes)},
                          {"address": UNCOUNTED_COPY_HELPER, "argc": 2, "argi": [0, 1],
                           "ret": "buf:output"}]
    return entry


def cases():
    """Separate actual nested-copy semantics from scripted call observations."""
    counts = [0, 1, 2, 3, 7, 8, 15, 16, 17, 20, 21, 22, 23, 24, 31, 32, 33,
              63, 64, 65, 127, 128, 129, 255, 256, 257, 65535, 65536, 65537]
    for scripted in [False, True]:
        family = "scripted" if scripted else "nested"
        for count in counts:
            yield case(f"{family}-count-{count}", count, scripted=scripted)
        for low in [0, 1, 2, 22, 23, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF]:
            for high in [0x100000000, 0xABCDEF1200000000, 0xFFFFFFFF00000000]:
                # INT32_MAX cannot fit a valid admitted destination here. A null
                # destination proves it is ignored before any helper or byte access.
                destination = "0x0" if low == 0x7FFFFFFF else None
                yield case(f"{family}-upper-{high:016x}-{low:08x}", high | low,
                           destination=destination, scripted=scripted,
                           size=128 if destination else None)
        for low in [0, 1, 2, 22, 23, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]:
            yield case(f"{family}-null-{low:08x}", low, destination="0x0", size=128,
                       scripted=scripted)
        for low in [0, 0x80000000, 0x80000001, 0xFFFFFFFF]:
            yield case(f"{family}-opaque-{low:08x}", low,
                       destination="0xfedcba9876543210", size=128, scripted=scripted)
        for offset in [0, 1, 3, 7, 15, 31]:
            for count in [1, 2, 21, 22, 23, 24]:
                yield case(f"{family}-unaligned-{offset}-{count}", count, offset=offset,
                           scripted=scripted)
        for register in ["rdi", "rsi", "r8", "r9"]:
            for value in ["0x0", "0x1", "0x100000000", "0x8000000000000000",
                          "0xffffffffffffffff", "buf:output+7"]:
                yield case(f"{family}-ignored-{register}-{value.replace(':', '-').replace('+', '-')}",
                           incoming={register: value}, scripted=scripted)
        for index, values in enumerate([["0x0"] * 4, ["0xffffffffffffffff"] * 4,
                                       ["buf:output", "0x8000000000000000", "0x1", "0x100000000"]]):
            yield case(f"{family}-ignored-stack-{index}", stack=values, scripted=scripted)
        for count in [1, 2, 21, 22, 23, 24, 64]:
            yield case(f"{family}-exact-{count}", count, offset=0, size=count,
                       scripted=scripted)
    # The signed -1 gate witness keeps a mistaken admitted final index -2 owned.
    yield case("scripted-negative-one-owned-index", 0xFFFFFFFF, offset=2, scripted=True)
    for count in [1, 2, 21, 22, 23, 24, 64, 257]:
        writes = [{"arg": 0, "offset": count - 1, "bytes": "ce"}]
        if count > 2:
            writes.insert(0, {"arg": 0, "offset": 0, "bytes": "b3c4"})
        yield case(f"scripted-overwrite-final-{count}", count, scripted=True, writes=writes,
                   helper_return="0xfedcba9876543210")
    for index, value in enumerate(["0x0", "0x1", "0x100000000", "0x8000000000000000",
                                    "0xffffffffffffffff", "buf:output+7"]):
        yield case(f"scripted-ignored-return-{index}", 23, scripted=True, helper_return=value)


def main():
    """Write deterministic synthetic inputs with unique IDs outside the repository."""
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
