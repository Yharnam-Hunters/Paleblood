#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic selective FMOD timing initialization. Usage: SCRIPT OUT_DIR.

Full owned-buffer observations distinguish the cleared fields from preserved
accumulation/output bytes, neighboring gates and surrounding storage. These
are deterministic edge inputs, without genuine game recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x01118740"
STATE_BYTES = 0x38
OWNER_BYTES = 0x200
CLEARED_FIELDS = ((0, 4), (4, 4), (8, 4), (0xc, 4), (0x10, 4),
                  (0x14, 4), (0x28, 1), (0x2c, 4), (0x30, 4), (0x34, 1))


def case(cid, data, offset=0x40):
    """Keep the typed object aligned and observe every surrounding byte."""
    assert len(data) == OWNER_BYTES
    assert offset % 4 == 0 and 0 <= offset <= OWNER_BYTES - STATE_BYTES
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
            "args": {"rdi": f"buf:owner+{offset}"},
            "buffers": {"owner": {"size": OWNER_BYTES, "bytes": bytes(data).hex()}},
            "memory": [], "imports": [], "stubs": []}


def cases():
    """Vary reset bits independently and retain arbitrary floating-point payloads."""
    rows = []
    for fill in (0, 1, 2, 0x7f, 0x80, 0xa5, 0xff):
        for offset in (0, 4, 0x40, 0xfc, OWNER_BYTES - STATE_BYTES):
            rows.append(case(f"fill-{fill:02x}-at-{offset:x}",
                             [fill] * OWNER_BYTES, offset))
    for field, width in CLEARED_FIELDS:
        values = (1, 2, 0x80, 0xff) if width == 1 else (
            1, 0x100, 0x10000, 0x1000000, 0x80000000, 0xffffffff)
        for value in values:
            data = bytearray([0xa5] * OWNER_BYTES)
            for other, size in CLEARED_FIELDS:
                data[0x40 + other:0x40 + other + size] = bytes(size)
            data[0x40 + field:0x40 + field + width] = value.to_bytes(width, "little")
            rows.append(case(f"field-{field:x}-value-{value:x}", data))
    for seed in (0, 1, 0x55, 0xff):
        data = bytearray((index * 73 + seed) & 255 for index in range(OWNER_BYTES))
        rows.append(case(f"distinct-bytes-{seed:02x}", data))
    # No FP operations occur: even signaling NaN payloads must survive bit-for-bit.
    for value in (0, 0x80000000, 0x3f800000, 0x7f800000, 0xff800000,
                  0x7fc01234, 0x7fa05678, 0xffffffff):
        data = bytearray([0xa5] * OWNER_BYTES)
        for field in (0x18, 0x1c, 0x20, 0x24):
            struct.pack_into("<I", data, 0x40 + field, value)
        rows.append(case(f"preserved-words-{value:08x}", data))
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
    print(f"wrote {len(rows)} synthetic timing initialization cases")


if __name__ == "__main__":
    main()
