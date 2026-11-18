#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD Thread counter contributions. Usage: SCRIPT OUT_DIR.

The original counter helper executes without scripted calls. Counter contexts
overlap owned Thread storage or a writable image word in selected cases. These
are deterministic edge inputs, without game recordings or real semaphore work.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x011186e0"
CONTRIBUTION = 0x05519b0c
WAKEUP = 0x130
STARTUP = 0x138
THREAD_BYTES = 0x158
COUNTER_BYTES = 0xc4
OWNER_BYTES = 0x800


def word(value, width=4):
    """Encode only the observed width of an unsigned field."""
    assert width in (4, 8) and 0 <= value < 1 << (width * 8)
    return struct.pack("<I" if width == 4 else "<Q", value).hex()


def case(cid, *, wakeup=1, startup=1, contribution=8, thread=0x100,
         counter=0x400, classified=0, total=0, fill=0xa5):
    """Place full-width handles and independently seeded additive DWORDs."""
    assert 0 <= thread <= OWNER_BYTES - THREAD_BYTES
    assert 0 <= fill <= 255
    pointer = lambda at: f"buf:owner+{at}"
    memory = []
    if isinstance(counter, int):
        assert 0 <= counter <= OWNER_BYTES - COUNTER_BYTES
        context = pointer(counter)
    elif counter == "null":
        context = "0x0"
    else:
        assert counter in ("image-classified", "image-total")
        offset = 8 if counter == "image-classified" else 0xc0
        address = CONTRIBUTION - offset
        context = f"guest:0x{address:x}"
        memory.append({"addr": f"0x{address:x}", "bytes": "00" * COUNTER_BYTES})
    if context != "0x0":
        def field(offset):
            return pointer(counter + offset) if isinstance(counter, int) else f"0x{address + offset:x}"
        memory.extend([{"addr": field(8), "bytes": word(classified)},
                       {"addr": field(0xc0), "bytes": word(total)}])
    # Handles and the mutable image word take precedence when presets overlap.
    memory.extend([{"addr": pointer(thread + WAKEUP), "bytes": word(wakeup, 8)},
                   {"addr": pointer(thread + STARTUP), "bytes": word(startup, 8)},
                   {"addr": f"0x{CONTRIBUTION:x}", "bytes": word(contribution)}])
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": pointer(thread), "rsi": context},
            "buffers": {"owner": {"size": OWNER_BYTES,
                                   "bytes": bytes([fill] * OWNER_BYTES).hex()}},
            "memory": memory, "imports": [], "stubs": []}


def cases():
    """Distinguish gates, mutable counts, wraparound and alias-sensitive reads."""
    rows = []
    handles = (0, 1, 2, 0xff, 0x100, 0x10000, 0x100000000,
               0x8000000000000000, 0xffffffffffffffff)
    for value in handles:
        rows.append(case(f"wakeup-{value:016x}", wakeup=value, startup=0))
        rows.append(case(f"startup-{value:016x}", wakeup=0, startup=value))
    for value in (0, 1, 2, 7, 8, 9, 0xff, 0x100, 0xffff, 0x10000,
                  0x7fffffff, 0x80000000, 0xffffffff):
        rows.append(case(f"contribution-{value:08x}", contribution=value))
    for classified in (0, 1, 0xfffffff8, 0xffffffff, 0x7fffffff, 0x80000000):
        for total in (0, 0xffffffff):
            rows.append(case(f"seeds-{classified:08x}-{total:08x}",
                             classified=classified, total=total))
    for wakeup, startup in ((0, 0), (1, 0), (0, 1), (1, 1)):
        rows.append(case(f"null-counter-{wakeup}-{startup}", counter="null",
                         wakeup=wakeup, startup=startup))
    for thread, counter in ((0, 0x400), (0x100, 0), (0x100, 0x401),
                            (OWNER_BYTES - THREAD_BYTES, 0),
                            (0, OWNER_BYTES - COUNTER_BYTES)):
        for fill in (0, 0x5a, 0xa5, 0xff):
            rows.append(case(f"placement-{thread:x}-{counter:x}-{fill:02x}",
                             thread=thread, counter=counter, fill=fill))
    # Each helper addition can independently create or clear the second gate.
    for offset, role in ((WAKEUP, "classified-startup"),
                         (STARTUP - 0xc0, "total-startup")):
        for startup in (0, 0xfffffff8, 0x100000000, 0xffffffff00000000):
            rows.append(case(f"alias-{role}-{startup:016x}",
                             counter=0x100 + offset, startup=startup))
    for wakeup in (1, 0xfffffff8, 0x100000000):
        rows.append(case(f"alias-classified-wakeup-{wakeup:016x}",
                         counter=0x100 + WAKEUP - 8, wakeup=wakeup))
    for counter in ("image-classified", "image-total"):
        for value in (0, 1, 3, 8, 0x80000000, 0xffffffff):
            rows.append(case(f"alias-{counter}-{value:08x}",
                             counter=counter, contribution=value))
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
    print(f"wrote {len(rows)} synthetic handle contribution cases")


if __name__ == "__main__":
    main()
