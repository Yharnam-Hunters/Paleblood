#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic cases for stopping the two threads through the output owner."""
import json
from pathlib import Path
import sys

import stop_output_threads_cases as callback


ADDRESS = "0x0111b9a0"


def direct(row):
    """The owner's address precedes the descriptor callback subobject."""
    argument = row["args"]["rdi"]
    buffer, offset = argument.split("+")
    assert buffer == "buf:owner"
    row["address"] = ADDRESS
    row["args"]["rdi"] = callback.pointer(int(offset) - callback.CALLBACK_BIAS)
    return row


def cases():
    """Compare shared shutdown paths, full status words and original helper effects."""
    for row in callback.cases():
        yield direct(row)

    # Controlled helper returns distinguish the width and polarity of each decision.
    statuses = (0, 1, 0x80, 0x100, 0x10000, 0x80000000, 0xffffffff,
                0x100000000, 0xabcdef0100000002)
    for first in statuses:
        for second in statuses:
            row = direct(callback.case(f"status-{first:x}-{second:x}"))
            for stub, status in zip(row["stubs"], (first, second)):
                stub["ret"] = hex(status)
            yield row

    distance = callback.MIXER_THREAD - callback.AUDIO_OUT_THREAD
    for value in (0, 1, 0x7f, 0xff):
        for first in (0, 33):
            row = direct(callback.case(f"mixer-write-{value:02x}-first-{first}"))
            row["stubs"][0]["writes"] = [
                {"arg": 0, "offset": distance + callback.ACTIVE, "bytes": f"{value:02x}"}]
            row["stubs"][1]["argmem"] = [{"arg": 0, "offset": callback.ACTIVE, "size": 2}]
            yield row

    # The real stop routine clears the running flag before a kernel failure.
    # AudioOut waits for startup; the mixer signals its optional wakeup first.
    for role, first, second in (("inactive", 0, 0), ("audio", 33, 0),
                                ("both", 33, 33), ("mixer", 0, 33)):
        for error in (1, 0x100, 0x80000000, 0xffffffff, 0xffffffff00000001):
            row = direct(callback.case(f"original-{role}-{error:x}", first, second))
            row["stubs"] = []
            row["imports"] = [
                {"name": "sceKernelWaitSema", "argc": 3, "argi": [0, 2],
                 "argu32": [1], "ret": hex(error)},
                {"name": "sceKernelSignalSema", "argc": 2, "argi": [0],
                 "argu32": [1], "ret": hex(error)}]
            yield row


def main():
    """Write deterministic synthetic fixtures to the requested directory."""
    if len(sys.argv) != 2:
        raise SystemExit("usage: stop_owner_threads_cases.py OUT_DIR")
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = list(cases())
    assert len(rows) == len({row["id"] for row in rows})
    for row in rows:
        (directory / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"wrote {len(rows)} synthetic output owner stop cases")


if __name__ == "__main__":
    main()
