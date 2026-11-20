#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD output close cases. Usage: SCRIPT OUT_DIR.

sceAudioOutOutput and sceAudioOutClose are scripted: a negative drain result, a
nonzero result from either close, and success through both. The pool release is
a scripted stub that receives the services pool, the buffer, the source file and
the line. Port handles and the second port's value are opaque to the function.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x0111b7c0"
RELEASE = "0x011ab3a0"
SERVICES = "0x056d6ae8"
AUDIO_OUT_PORT = 0x6AC
MIX_BUFFER = 0x6C8
SECOND_PORT = 0x980
OWNER_BYTES = 0xA00  # the second port plus the largest owner offset
POOL = 0x08
DRAIN_RESULTS = [0, 1, 0x7FFFFFFF]
FAILED_DRAINS = [-1, -0x7FD9FFFF, -0x80000000]
FAILED_CLOSES = [1, -1, -0x7FD9FFFE, 0x7FFFFFFF]
PORTS = [1, 0x100, 0x7FFFFFFF, -1, -0x80000000]


def dword(value):
    """Little-endian signed DWORD."""
    return struct.pack("<i", value).hex()


def output(ret):
    return {"name": "sceAudioOutOutput", "argc": 2, "ret": ret}


def close(ret):
    return {"name": "sceAudioOutClose", "argc": 1, "ret": ret}


def case(cid, port=0, second=0, buffer=True, drain=None, first_close=None,
         second_close=None, fill="a5", owner_offset=16):
    """One call; the scripted imports stop at the first failure the original sees."""
    imports = []
    if port != 0:
        imports.append(output(drain))
        if drain >= 0:
            imports.append(close(first_close))
            if first_close == 0:
                imports.append(close(second_close))
    failed = port != 0 and (drain < 0 or first_close != 0 or second_close != 0)
    memory = [
        {"addr": SERVICES, "pointer": "services"},
        {"addr": f"buf:services+{POOL}", "pointer": "pool"},
        {"addr": f"buf:owner+{owner_offset + AUDIO_OUT_PORT}", "bytes": dword(port)},
        {"addr": f"buf:owner+{owner_offset + SECOND_PORT}", "bytes": dword(second)},
        ({"addr": f"buf:owner+{owner_offset + MIX_BUFFER}", "pointer": "mix"} if buffer
         else {"addr": f"buf:owner+{owner_offset + MIX_BUFFER}", "bytes": "00" * 8}),
    ]
    stubs = []
    if buffer and not failed:
        stubs.append({"address": RELEASE, "argc": 4, "argi": [0, 1, 2, 3], "ret": 0})
    return {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
            "args": {"rdi": f"buf:owner+{owner_offset}", "rsi": "0xfedcba9876543210",
                     "rdx": "0xffffffffffffffff"},
            "buffers": {"owner": {"size": OWNER_BYTES, "bytes": fill * OWNER_BYTES},
                        "services": {"size": 32, "bytes": "c3" * 32},
                        "pool": {"size": 32, "bytes": "d4" * 32},
                        "mix": {"size": 64, "bytes": "e5" * 64}},
            "memory": memory, "imports": imports, "stubs": stubs}


def cases():
    """Every exit, with port values, fills and owner offsets around them."""
    for buffer in [True, False]:
        tag = "buffer" if buffer else "nobuffer"
        for second in [0] + PORTS:
            yield case(f"closed-{tag}-second-{second & 0xFFFFFFFF:08x}", second=second,
                       buffer=buffer)
        for port in PORTS:
            p = f"{port & 0xFFFFFFFF:08x}"
            for drain in FAILED_DRAINS:
                yield case(f"drain-fails-{tag}-{p}-{drain & 0xFFFFFFFF:08x}", port, 7, buffer,
                           drain)
            for result in FAILED_CLOSES:
                r = f"{result & 0xFFFFFFFF:08x}"
                yield case(f"first-close-fails-{tag}-{p}-{r}", port, 7, buffer, 0, result)
                yield case(f"second-close-fails-{tag}-{p}-{r}", port, 7, buffer, 0, 0, result)
            for drain in DRAIN_RESULTS:
                for second in [0, 7, -1]:
                    yield case(f"closes-{tag}-{p}-{drain:08x}-{second & 0xFFFFFFFF:08x}",
                               port, second, buffer, drain, 0, 0)
    for fill in ["00", "01", "7f", "ff"]:
        yield case(f"fill-{fill}-closed", fill=fill)
        yield case(f"fill-{fill}-closes", 3, 4, True, 0, 0, 0, fill=fill)
        yield case(f"fill-{fill}-fails", 3, 4, True, 0, 0, 1, fill=fill)
    for offset in [0, 8, 24, 32, 64]:
        yield case(f"offset-{offset}-closes", 3, 4, True, 0, 0, 0, owner_offset=offset)
        yield case(f"offset-{offset}-closed", owner_offset=offset)


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
