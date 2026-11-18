#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD Thread wake cases. Usage: SCRIPT OUT_DIR.

The original semaphore wrapper executes; its kernel signal import is scripted.
Opaque handles need not designate readable storage. Fixed-location kernel writes
model visible side effects, without claiming real synchronization or recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x011186c0"
WAKEUP = 0x130
STARTUP = 0x138
THREAD_BYTES = 0x158
OWNER_BYTES = 0x400


def word(value):
    """Encode a complete stored handle without narrowing its upper bits."""
    assert 0 <= value <= 0xffffffffffffffff
    return struct.pack("<Q", value).hex()


def case(cid, *, handle=0x100000001, result=0, offset=32, fill=0xa5,
         incoming=0, write=None, startup=0xfedcba9876543210):
    """Provide observing imports even when the correct null path skips them."""
    assert offset in (0, 8, 16, 32, 64, 128)
    assert offset + THREAD_BYTES <= OWNER_BYTES and 0 <= fill <= 255
    assert write in (None, "clear-wakeup", "replace-wakeup", "replace-startup", "running")

    def address(field):
        return f"buf:owner+{offset + field}"

    memory = [{"addr": address(WAKEUP), "bytes": word(handle)},
              {"addr": address(STARTUP), "bytes": word(startup)}]
    writes = []
    if write is not None:
        field, value = {
            "clear-wakeup": (WAKEUP, "00" * 8),
            "replace-wakeup": (WAKEUP, word(0x8000000200000000)),
            "replace-startup": (STARTUP, word(0x1234567800000000)),
            "running": (0x118, "7f"),
        }[write]
        writes.append({"addr": address(field), "bytes": value})
    signal = {"name": "sceKernelSignalSema", "argc": 2, "argi": [0],
              "argu32": [1], "ret": hex(result & 0xffffffffffffffff), "writes": writes}
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": address(0), "rsi": hex(incoming & 0xffffffffffffffff)},
            "buffers": {"owner": {"size": OWNER_BYTES,
                                   "bytes": bytes([fill] * OWNER_BYTES).hex()}},
            "memory": memory,
            "imports": [signal, {"name": "sceKernelSignalSema", "argc": 2,
                                 "argi": [0], "argu32": [1], "ret": "0x21"}],
            "stubs": []}


def cases():
    """Distinguish full-handle gating, fixed count and DWORD error translation."""
    handles = (0, 1, 2, 0x80, 0xff, 0x100, 0x10000, 0x1000000,
               0x100000000, 0x8000000000000000, 0x100000001,
               0x8000000200000000, 0xffffffffffffffff)
    for handle in handles:
        yield case(f"handle-{handle:016x}", handle=handle)
    for result in (0, 1, 2, 0x7f, 0x80, 0xff, 0x100, 0x10000,
                   0x1000000, 0x80000000, 0xffffffff, 0x100000000,
                   0x8000000000000000, 0xffffffff00000000,
                   0x8000000100000001, 0xffffffffffffffff):
        yield case(f"kernel-result-{result:016x}", result=result)
    for offset in (0, 8, 16, 32, 64, 128):
        for fill in (0, 0x5a, 0xa5, 0xff):
            yield case(f"placement-{offset}-fill-{fill:02x}", offset=offset, fill=fill)
    for incoming in (0, 1, 2, 0x7f, 0x80, 0xff, 0x100, 0x100000000,
                     0xffffffff00000002, 0xffffffffffffffff):
        yield case(f"unused-rsi-{incoming:016x}", incoming=incoming)
    for write in ("clear-wakeup", "replace-wakeup", "replace-startup", "running"):
        for result in (0, 0x80000000, 0xffffffff00000000):
            yield case(f"kernel-write-{write}-{result:016x}", write=write, result=result)
    for startup in (0, 1, 0x100000000, 0xffffffffffffffff):
        yield case(f"null-wakeup-startup-{startup:016x}", handle=0, startup=startup,
                   write="running", result=0x80000000)


def main():
    """Write deterministic fixtures and reject duplicate IDs before emission."""
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = list(cases())
    assert len({row["id"] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"wrote {len(rows)} synthetic wake cases")


if __name__ == "__main__":
    main()
