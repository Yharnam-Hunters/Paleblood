#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD output port-state cases. Usage: SCRIPT OUT_DIR.

sceAudioOutGetPortState is scripted: its result (ignored by the function) and the
32-byte state it writes, of which only the 16-bit output word matters. The two
listener updates are scripted stubs; which one runs depends on bit 0x20 of the
service context's flag byte. The state buffer is on the function's own stack, so
only the port handle's low DWORD is compared for that call.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x0111bb10"
UPDATE_FLAGGED = "0x01134090"
UPDATE_UNFLAGGED = "0x01133f80"
SERVICE_CONTEXT = 0x60
LISTENER = 0x70
SECOND_PORT = 0x980
DISCONNECTED = 0x984
OWNER_BYTES = 0xA00  # the cached byte plus the largest owner offset
FLAGS = 0x32
LISTENER_UPDATE_FLAG = 0x20


def qword(value):
    return struct.pack("<Q", value).hex()


def case(cid, listener=True, port=7, output=1, cached=0, flags=0x00, result=0,
         state_fill="5a", fill="a5", owner_offset=16):
    """One poll; the scripted calls follow the path the original takes."""
    base = owner_offset
    memory = [
        {"addr": f"buf:owner+{base + SERVICE_CONTEXT}", "pointer": "service"},
        ({"addr": f"buf:owner+{base + LISTENER}", "pointer": "listener"} if listener
         else {"addr": f"buf:owner+{base + LISTENER}", "bytes": qword(0)}),
        {"addr": f"buf:owner+{base + SECOND_PORT}", "bytes": struct.pack("<i", port).hex()},
        {"addr": f"buf:owner+{base + DISCONNECTED}", "bytes": f"{cached:02x}"},
        {"addr": f"buf:service+{FLAGS}", "bytes": f"{flags:02x}"},
    ]
    imports, stubs = [], []
    if listener and port != 0:
        state = struct.pack("<H", output) + bytes.fromhex(state_fill) * 30
        imports.append({"name": "sceAudioOutGetPortState", "argc": 2, "argi": [], "argu32": [0],
                        "ret": hex(result & 0xFFFFFFFFFFFFFFFF),
                        "writes": [{"arg": 1, "offset": 0, "bytes": state.hex(), "size": 32}]})
        disconnected = int(output == 0)
        if cached != disconnected:
            target = UPDATE_FLAGGED if flags & LISTENER_UPDATE_FLAG else UPDATE_UNFLAGGED
            stubs.append({"address": target, "argc": 3, "argi": [0], "argu8": [1, 2], "ret": 0})
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": f"buf:owner+{owner_offset}", "rsi": "0xfedcba9876543210"},
            "buffers": {"owner": {"size": OWNER_BYTES, "bytes": fill * OWNER_BYTES},
                        "service": {"size": 64, "bytes": "c3" * 64},
                        "listener": {"size": 64, "bytes": "d4" * 64}},
            "memory": memory, "imports": imports, "stubs": stubs}


def cases():
    """Every path, with cached bytes, output bits, results, flags and placements around them."""
    for port in [0, 7, -1]:
        yield case(f"no-listener-port-{port & 0xFFFFFFFF:08x}", listener=False, port=port)
    for cached in [0, 1, 2]:
        yield case(f"port-closed-cached-{cached:02x}", port=0, cached=cached)
    for output in [0, 1, 2, 0x8000, 0xFFFF]:
        for cached in [0, 1, 2, 0x80, 0xFF]:
            for flags in [0x00, 0x20, 0x1F, 0xDF, 0xFF]:
                yield case(f"output-{output:04x}-cached-{cached:02x}-flags-{flags:02x}",
                           output=output, cached=cached, flags=flags)
    for result in [0, 1, -1, -0x7FD9FFFF]:
        for output in [0, 1]:
            disconnected = int(output == 0)
            for changed in [False, True]:
                yield case(f"result-{result & 0xFFFFFFFF:08x}-output-{output}-"
                           f"{'changed' if changed else 'same'}", output=output,
                           cached=1 - disconnected if changed else disconnected, result=result)
    for port in [1, 0x7FFFFFFF, -1, -0x80000000]:
        yield case(f"port-{port & 0xFFFFFFFF:08x}-changed", port=port, output=0, cached=0)
        yield case(f"port-{port & 0xFFFFFFFF:08x}-same", port=port, output=0, cached=1)
    for state_fill in ["00", "ff"]:
        for output in [0, 1]:
            yield case(f"state-{state_fill}-output-{output}", output=output, state_fill=state_fill)
    for fill in ["00", "01", "7f", "ff"]:
        yield case(f"fill-{fill}-changed", output=0, cached=0, flags=0x20, fill=fill)
        yield case(f"fill-{fill}-same", output=1, cached=0, fill=fill)
    for offset in [0, 8, 24, 32, 64]:
        yield case(f"offset-{offset}-changed", output=0, cached=0, owner_offset=offset)
        yield case(f"offset-{offset}-same", output=0, cached=1, owner_offset=offset)


def main():
    """Write deterministic unique synthetic inputs with hyphen-only IDs."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len(generated) == len({row["id"] for row in generated})
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
