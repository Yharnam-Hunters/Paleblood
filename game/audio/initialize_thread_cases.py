#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD Thread initialization cases. Usage: SCRIPT OUT_DIR.

Only owned object bytes are initialized. The mapped, relocation-populated vtable
is left intact. Full buffer comparisons observe both the selected clears and the
name, service context, padding and surrounding bytes that the original preserves.
There are no scripted calls, imports or recordings from a game run.
"""
import json
from pathlib import Path
import sys


ADDRESS = "0x011183b0"
THREAD_BYTES = 0x158
OWNER_BYTES = 0x200
CLEARED_FIELDS = (("handle", 0x110, 8), ("running", 0x118, 1),
                  ("argument", 0x120, 8), ("unknown-128", 0x128, 8),
                  ("wakeup", 0x130, 8), ("startup", 0x138, 8),
                  ("entry", 0x140, 8), ("delay", 0x150, 4))
PRESERVED_REGIONS = (("byte-8", 8, 1), ("name", 9, 256),
                     ("padding-109", 0x109, 7), ("padding-119", 0x119, 7),
                     ("context", 0x148, 8), ("tail", 0x154, 4))


def case(cid, fill=0xA5, thread_offset=32, pattern=None, patches=(), incoming=None, stack=()):
    """Place an aligned Thread inside a bounded owner with independent guard bytes."""
    assert thread_offset % 8 == 0 and 0 <= thread_offset <= 64
    assert thread_offset + THREAD_BYTES <= OWNER_BYTES
    raw = bytearray([fill] * OWNER_BYTES)
    if pattern is not None:
        multiplier, seed = pattern
        raw[:] = bytes((i * multiplier + seed) & 0xFF for i in range(OWNER_BYTES))
    for offset, data in patches:
        assert 0 <= offset and offset + len(data) <= THREAD_BYTES
        raw[thread_offset + offset:thread_offset + offset + len(data)] = data
    args = {"rdi": f"buf:owner+{thread_offset}", "rsi": "0xffffffffffffffff",
            "rdx": "0x8000000000000001", "rcx": "0xfedcba9876543210",
            "r8": "0x100000000", "r9": "0x1"}
    assert not set(incoming or {}) - {"rsi", "rdx", "rcx", "r8", "r9", "rax"}
    args.update(incoming or {})
    row = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void", "args": args,
           "buffers": {"owner": {"size": OWNER_BYTES, "bytes": raw.hex()}},
           "memory": [], "imports": [], "stubs": []}
    if stack:
        row["stack"] = list(stack)
    return row


def cases():
    """Vary field widths, preserved neighbors, object placement and unused inputs."""
    for offset in (0, 8, 24, 64):
        for fill in (0, 1, 0x7F, 0x80, 0xA5, 0xFF):
            yield case(f"fill-{fill:02x}-offset-{offset}", fill=fill, thread_offset=offset)
    for multiplier, seed in ((1, 0), (3, 17), (17, 41), (127, 128), (255, 255)):
        yield case(f"pattern-{multiplier}-{seed}", pattern=(multiplier, seed))
    for name, offset, width in CLEARED_FIELDS:
        yield case(name + "-low-only", fill=0,
                   patches=((offset, b"\x01" + bytes(width - 1)),))
        yield case(name + "-high-only", fill=0,
                   patches=((offset, bytes(width - 1) + b"\x80"),))
    for name, offset, width in PRESERVED_REGIONS:
        yield case(name + "-first", fill=0, patches=((offset, b"\x81"),))
        yield case(name + "-last", fill=0, patches=((offset + width - 1, b"\xFE"),))
    for reg in ("rsi", "rdx", "rcx", "r8", "r9", "rax"):
        for index, value in enumerate(("0x0", "0x100000000", "0xffffffffffffffff", "buf:owner+72")):
            yield case(f"ignored-{reg}-{index}", incoming={reg: value})
    for index, stack in enumerate((("0x0",) * 4, ("0xffffffffffffffff",) * 4,
                                   ("buf:owner+0", "0x100000000", "buf:owner+96", "0x80"))):
        yield case(f"ignored-stack-{index}", stack=stack)


def main():
    """Write deterministic fixture dictionaries to the requested private directory."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    made = list(cases())
    assert len(made) == len({row["id"] for row in made})
    for fixture in made:
        (output / (fixture["id"] + ".json")).write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(made)} synthetic cases written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
