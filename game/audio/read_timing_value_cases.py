#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic timing-scalar copy cases. Usage: SCRIPT OUT_DIR.

No clock, playback or genuine game recording is involved. Payloads are raw words;
the observed operation copies them without floating-point arithmetic.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x01118870"
OWNER_BYTES = 1024
STATE_BYTES = 0x38
VALUE = 0x24


def case(cid, *, payload=0x67452301, state=64, output=768, fill=0xa5, pattern=False):
    """Use one complete owned snapshot, including exact and partial aliases."""
    assert 0 <= fill <= 255 and 0 <= payload <= 0xffffffff
    memory = bytearray(((i * 37 + fill) & 255) if pattern else fill
                       for i in range(OWNER_BYTES))
    if isinstance(state, int):
        assert state >= 0 and state + STATE_BYTES <= OWNER_BYTES
        struct.pack_into("<I", memory, state + 0x20, 0x89abcdef)
        struct.pack_into("<I", memory, state + 0x28, 0x10325476)
        struct.pack_into("<I", memory, state + VALUE, payload)
        source = f"buf:owner+{state}"
    else:
        assert output is None and state in ("0x0", "0x1", "0xffffffffffffffff")
        source = state
    if output is not None:
        assert isinstance(output, int) and 0 <= output <= OWNER_BYTES - 4
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": source, "rsi": "0x0" if output is None else f"buf:owner+{output}"},
            "buffers": {"owner": {"size": OWNER_BYTES, "bytes": memory.hex()}},
            "memory": [], "imports": [], "stubs": []}


def cases():
    """Vary IEEE payloads, both alias directions, full pointers and skipped reads."""
    payloads = (0x00000000, 0x80000000, 0x00000001, 0x80000001,
                0x007fffff, 0x807fffff, 0x00800000, 0x80800000,
                0x3f000000, 0xbf000000, 0x3f800000, 0xbf800000,
                0x40000000, 0xc0000000, 0x7f7fffff, 0xff7fffff,
                0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000,
                0x7fc12345, 0xffc12345, 0x7f800001, 0xff800001,
                0x7fa54321, 0xffa54321, 0x01234567, 0x89abcdef,
                0xaaaaaaaa, 0x55555555, 0xdeadbeef, 0x67452301)
    for payload in payloads:
        yield case(f"payload-{payload:08x}", payload=payload)
    for payload in (0x67452301, 0xf1d3b597):
        for delta in range(-4, 5):
            yield case(f"alias-{payload:08x}-{delta:+d}", payload=payload,
                       state=128, output=128 + VALUE + delta, pattern=True)
    for state in (0, 4, 64, 128, 512, OWNER_BYTES - STATE_BYTES):
        for fill in (0, 0x5a, 0xff):
            yield case(f"placement-{state}-fill-{fill:02x}", state=state,
                       payload=0x12345678 ^ state, output=OWNER_BYTES - 4, fill=fill)
    for state in (0, 64, OWNER_BYTES - STATE_BYTES):
        for fill in (0, 0xff):
            yield case(f"null-output-{state}-{fill:02x}", state=state, output=None, fill=fill)
    for state in ("0x0", "0x1", "0xffffffffffffffff"):
        yield case("null-output-unread-state-" + state[2:], state=state, output=None)
    for output in (0, 256, 512):
        yield case(f"full-output-pointer-{output}", output=output, pattern=True)


def main():
    """Write deterministic complete cases and reject duplicate IDs."""
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = list(cases())
    assert len(rows) == 80 and len({row["id"] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"wrote {len(rows)} synthetic timing-value cases")


if __name__ == "__main__":
    main()
