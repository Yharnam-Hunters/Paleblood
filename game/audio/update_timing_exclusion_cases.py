#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic nested timing-exclusion cases. Usage: SCRIPT OUT_DIR.

Scripted clock-entry cases write only the documented output DWORD on success.
Alias cases execute the original clock with only its kernel import scripted.
These are synthetic cases, with no real clock samples or game recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x01118890"
CLOCK = "0x0111a760"
CLOCK_STATE_POINTER = "0x056d6ae8"
CLOCK_BASE_SECONDS = 0x10
CLOCK_ERROR = 0x21
OWNER_BYTES = 0x200
STATE_BYTES = 0x38
START = 0x08
END = 0x0c
EXCLUDING = 0x28
EXCLUDED = 0x2c
DEPTH = 0x30
RUNNING = 0x34


def case(cid, *, mode=1, running=1, depth=0, start=17, end=31,
         excluded=0x10203040, tick=100, error=False, offset=0x40,
         fill=0xa5, initial_excluding=0x5a):
    """Observe the complete owner and install clock observers on every path."""
    assert offset % 4 == 0 and 0 <= offset <= OWNER_BYTES - STATE_BYTES
    assert 0 <= mode <= 0xffffffffffffffff
    assert all(0 <= value <= 0xffffffff
               for value in (depth, start, end, excluded, tick))
    assert all(0 <= value <= 255 for value in (running, fill, initial_excluding))
    data = bytearray((index * 37 + fill) & 255 for index in range(OWNER_BYTES))
    for field, value in ((START, start), (END, end), (EXCLUDED, excluded), (DEPTH, depth)):
        struct.pack_into("<I", data, offset + field, value)
    data[offset + EXCLUDING] = initial_excluding
    data[offset + RUNNING] = running
    writes = [] if error else [{"arg": 0, "offset": 0,
                                "bytes": struct.pack("<I", tick).hex()}]
    observer = {"address": CLOCK, "argc": 1, "argi": [0],
                "ret": hex(CLOCK_ERROR if error else 0), "writes": writes}
    extra = {"address": CLOCK, "argc": 1, "argi": [0],
             "ret": hex(CLOCK_ERROR), "writes": []}
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": f"buf:owner+{offset}", "rsi": hex(mode),
                     "rax": "0xfedcba9876543210"},
            "buffers": {"owner": {"size": OWNER_BYTES, "bytes": data.hex()}},
            "memory": [], "imports": [], "stubs": [observer, extra]}


def clock_alias(cid, *, field, mode, seconds=2, nanoseconds=123456789,
                error=False, null_clock=False):
    """Overlap the original clock's base with a proven timing-storage DWORD."""
    assert field in (START, EXCLUDING, EXCLUDED, DEPTH)
    depth = 0 if mode & 255 else 1
    row = case(cid, mode=mode, depth=depth, start=0, excluded=0)
    offset = 0x40
    data = bytearray.fromhex(row["buffers"]["owner"]["bytes"])
    # A final exit decrements depth to zero before calling the clock. Every
    # other aliased base starts as a complete zero DWORD, including padding.
    initial = depth if field == DEPTH else 0
    struct.pack_into("<I", data, offset + field, initial)
    row["buffers"]["owner"]["bytes"] = data.hex()
    row["memory"] = [{"addr": CLOCK_STATE_POINTER, "bytes": "00" * 8} if null_clock else {
        "addr": CLOCK_STATE_POINTER,
        "pointer": f"owner+{offset + field - CLOCK_BASE_SECONDS}"}]
    row["stubs"] = []
    writes = [] if error else [{"arg": 1, "offset": 0, "size": 16,
                                "bytes": struct.pack("<qq", seconds, nanoseconds).hex()}]
    row["imports"] = [
        {"name": "sceKernelClockGettime", "argc": 2,
         "ret": 1 if error else 0, "writes": writes},
        {"name": "sceKernelClockGettime", "argc": 2, "ret": 1, "writes": []},
    ]
    return row


def cases():
    """Distinguish raw gates, nesting, unsigned intervals, failures and re-reads."""
    rows = []
    depths = (0, 1, 2, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff)
    for mode in (0, 1, 2, 0x80, 0xff, 0x100, 0x100000000):
        for depth in (0, 1, 0xffffffff):
            rows.append(case(f"inactive-{mode:x}-{depth:08x}", running=0,
                             mode=mode, depth=depth))
    for mode in (1, 2, 0x80, 0xff):
        for depth in depths:
            rows.append(case(f"enter-{mode:02x}-{depth:08x}", mode=mode, depth=depth))
    for mode in (0, 0x100, 0x10000, 0x100000000, 0xffffff00):
        for depth in depths:
            rows.append(case(f"exit-{mode:x}-{depth:08x}", mode=mode, depth=depth))
    intervals = ((0, 0), (0, 1), (1, 0), (1, 1), (1, 2),
                 (0x7fffffff, 0x80000000), (0x80000000, 0x7fffffff),
                 (0, 0xffffffff), (0xffffffff, 0),
                 (0xfffffffe, 0xffffffff), (0xffffffff, 0xfffffffe))
    for start, tick in intervals:
        for excluded in (0, 0xffffffff):
            rows.append(case(f"interval-{start:08x}-{tick:08x}-{excluded:08x}",
                             mode=0, depth=1, start=start, tick=tick, excluded=excluded))
    for mode, depth in ((1, 0), (2, 0), (0, 1)):
        for start, end in ((0, 100), (100, 0), (17, 17), (0x80000000, 0xffffffff)):
            rows.append(case(f"failure-{mode}-{start:08x}-{end:08x}",
                             mode=mode, depth=depth, start=start, end=end, error=True))
    for running in (2, 0x80, 0xff):
        for mode, depth in ((2, 0), (0, 1)):
            rows.append(case(f"running-{running:02x}-{mode}", running=running,
                             mode=mode, depth=depth))
    for offset in (0, 4, OWNER_BYTES - STATE_BYTES):
        for fill in (0, 0xff):
            for mode, depth in ((1, 0), (0, 1)):
                rows.append(case(f"placement-{offset:x}-{fill:02x}-{mode}",
                                 offset=offset, fill=fill, mode=mode, depth=depth))
    for field in (START, EXCLUDING, EXCLUDED, DEPTH):
        for mode in (0, 2):
            for seconds in (2, 0x12345678):
                rows.append(clock_alias(f"clock-alias-{field:x}-{mode}-{seconds:x}",
                                        field=field, mode=mode, seconds=seconds))
    for mode in (0, 2):
        rows.append(clock_alias(f"clock-failure-{mode}", field=DEPTH, mode=mode, error=True))
        rows.append(clock_alias(f"clock-null-global-failure-{mode}", field=DEPTH,
                                mode=mode, error=True, null_clock=True))
    return rows


def main():
    """Write deterministic fixtures and reject duplicate identities."""
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row["id"] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"wrote {len(rows)} synthetic timing-exclusion cases")


if __name__ == "__main__":
    main()
