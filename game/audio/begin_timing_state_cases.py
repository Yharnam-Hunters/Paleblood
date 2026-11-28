#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD timing-begin cases. Usage: SCRIPT OUT_DIR.

Most cases script the established clock at its original entry: success writes
one DWORD and error leaves it intact. Additional alias cases execute the real
clock with only its kernel import scripted, overlapping the shared clock base
with the running byte. These do not acquire real clock samples or constitute
genuine game recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x01118770"
CLOCK = "0x0111a760"
CLOCK_ERROR = 0x21
CLOCK_STATE_POINTER = "0x056d6ae8"
CLOCK_BASE_SECONDS = 0x10
STATE_BYTES = 0x38
OWNER_BYTES = 0x200
RUNNING = 0x34


def case(cid, *, tick=0x12345678, initial=0x87654321, running=0xa5,
         error=False, offset=0x40, fill=0xa5, incoming=0, preserved=None,
         pattern=None):
    """Observe the clock even when deliberately wrong code skips its call."""
    assert offset % 4 == 0 and 0 <= offset <= OWNER_BYTES - STATE_BYTES
    assert 0 <= tick <= 0xffffffff and 0 <= initial <= 0xffffffff
    assert 0 <= running <= 255 and 0 <= fill <= 255
    data = bytearray([fill] * OWNER_BYTES) if pattern is None else bytearray(pattern)
    assert len(data) == OWNER_BYTES
    struct.pack_into("<I", data, offset, initial)
    data[offset + RUNNING] = running
    if preserved is not None:
        for field in (0x18, 0x1c, 0x20, 0x24):
            struct.pack_into("<I", data, offset + field, preserved)
    writes = [] if error else [{"arg": 0, "offset": 0,
                                "bytes": struct.pack("<I", tick).hex()}]
    clock = {"address": CLOCK, "argc": 1, "argi": [0],
             "ret": hex(CLOCK_ERROR if error else 0), "writes": writes}
    # A second observer makes a duplicate call a finite trace difference.
    extra = {"address": CLOCK, "argc": 1, "argi": [0],
             "ret": hex(CLOCK_ERROR), "writes": []}
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": f"buf:owner+{offset}", "rsi": hex(incoming),
                     "rax": hex(incoming)},
            "buffers": {"owner": {"size": OWNER_BYTES, "bytes": data.hex()}},
            "memory": [], "imports": [], "stubs": [clock, extra]}


def clock_base_alias(cid, *, seconds=2, nanoseconds=123456789,
                     error=False, null_clock=False):
    """Execute the original clock with its base DWORD overlapping running/padding."""
    offset = 0x40
    row = case(cid, offset=offset)
    data = bytearray.fromhex(row["buffers"]["owner"]["bytes"])
    initial_base = 0xabcdef80 if error else 0
    struct.pack_into("<I", data, offset + RUNNING, initial_base)
    row["buffers"]["owner"]["bytes"] = data.hex()
    pointer = {"addr": CLOCK_STATE_POINTER, "bytes": "00" * 8} if null_clock else {
        "addr": CLOCK_STATE_POINTER,
        "pointer": f"owner+{offset + RUNNING - CLOCK_BASE_SECONDS}"}
    row["memory"] = [pointer]
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
    """Vary clock writes, preserved failures, raw gates and surrounding bytes."""
    rows = []
    ticks = (0, 1, 2, 0xff, 0x100, 0xffff, 0x10000, 0x7fffffff,
             0x80000000, 0xfffffffe, 0xffffffff)
    for tick in ticks:
        for running in (0, 1, 2, 0xff):
            rows.append(case(f"clock-{tick:08x}-running-{running:02x}",
                             tick=tick, running=running))
    for initial in (0, 1, 0x100, 0x10000, 0x1000000, 0x7fffffff,
                    0x80000000, 0xffffffff):
        for running in (0, 1, 2, 0x7f, 0x80, 0xff):
            rows.append(case(f"error-initial-{initial:08x}-running-{running:02x}",
                             error=True, initial=initial, running=running))
    for offset in (0, 4, 0x40, 0xfc, OWNER_BYTES - STATE_BYTES):
        for fill in (0, 0x5a, 0xa5, 0xff):
            for error in (False, True):
                rows.append(case(f"placement-{offset:x}-{fill:02x}-error-{int(error)}",
                                 offset=offset, fill=fill, error=error))
    for incoming in (0, 1, 0x100000000, 0x8000000000000000, 0xffffffffffffffff):
        for error in (False, True):
            rows.append(case(f"unused-{incoming:016x}-error-{int(error)}",
                             incoming=incoming, error=error))
    for value in (0, 0x80000000, 0x3f800000, 0x7f800000, 0xff800000,
                  0x7fc01234, 0x7fa05678, 0xffffffff):
        for error in (False, True):
            rows.append(case(f"preserved-{value:08x}-error-{int(error)}",
                             preserved=value, error=error))
    for seed in (0, 1, 0x55, 0xff):
        pattern = [(index * 73 + seed) & 255 for index in range(OWNER_BYTES)]
        for error in (False, True):
            rows.append(case(f"distinct-{seed:02x}-error-{int(error)}",
                             pattern=pattern, error=error))
    # The clock initializes its shared base before writing the output. Moving
    # the running store ahead of the call changes that base and the final tick.
    for seconds in (1, 2, 0x01020304, 0x12345678):
        for nanoseconds in (0, 123456789):
            rows.append(clock_base_alias(f"clock-base-alias-{seconds:08x}-{nanoseconds}",
                                         seconds=seconds, nanoseconds=nanoseconds))
    rows.append(clock_base_alias("clock-base-alias-error", error=True))
    rows.append(clock_base_alias("clock-null-global-error", error=True, null_clock=True))
    return rows


def main():
    """Write deterministic fixtures after checking unique case identities."""
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row["id"] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"wrote {len(rows)} synthetic timing begin cases")


if __name__ == "__main__":
    main()
