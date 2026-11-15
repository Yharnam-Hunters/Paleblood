#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD output-thread stop cases. Usage: SCRIPT OUT_DIR.

Only the descriptor wrapper executes. Its original Thread helper is scripted:
inactive objects return zero without effects; an active AudioOut uses a startup
wait failure, and an active mixer uses its optional signal failure. Those early
error paths clear only the active byte and return 33. Owned opaque semaphore
storage does not establish real OS semaphore state or thread execution.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x0111b1f0"
STOP = "0x011185a0"
CALLBACK_BIAS = 0x38
AUDIO_OUT_THREAD = 0x6D0
MIXER_THREAD = 0x828
THREAD_VTABLE = "0x056ce930"
ACTIVE = 0x118
WAKEUP = 0x130
STARTUP = 0x138
OWNER_BYTES = 0xB00
STATUSES = [0, 33]


def pointer(offset):
    """Use symbolic owned addresses rather than copying sandbox addresses."""
    return f"buf:owner+{offset}"


def word(value):
    """Write exact little-endian bits in owned storage."""
    return struct.pack("<Q", value).hex()


def script(status):
    """Observe the full logical pointer and exact minimal helper effects."""
    assert status in STATUSES
    return {"address": STOP, "argc": 1, "argi": [0], "ret": hex(status),
            "writes": ([{"arg": 0, "offset": ACTIVE, "bytes": "00"}] if status else [])}


def case(cid, first=0, second=0, owner_offset=16, active_byte=1,
         neighbor_byte=0xA5, fill="a5", native_handle=0,
         incoming=None, stack=(), wake_alias=False, startup_alias=False):
    """Supply valid owned objects and per-role reachable 0/33 early paths."""
    assert first in STATUSES and second in STATUSES
    assert owner_offset in [0, 8, 16, 24, 32, 64]
    assert 0 < active_byte <= 255 and 0 <= neighbor_byte <= 255
    memory = [{"addr": pointer(owner_offset + 0x60), "bytes": "00" * 8}]
    for thread, status, entry, name in [
            (AUDIO_OUT_THREAD, first, "0x0111b920", "synthetic AudioOut"),
            (MIXER_THREAD, second, "0x0111b910", "synthetic mixer")]:
        base = owner_offset + thread
        memory += [{"addr": pointer(base), "guest": THREAD_VTABLE},
                   {"addr": pointer(base + 9), "bytes": (name.encode() + b"\0").hex()},
                   {"addr": pointer(base + 0x110), "bytes": word(native_handle)},
                   {"addr": pointer(base + ACTIVE),
                    "bytes": bytes([active_byte if status else 0, neighbor_byte]).hex()},
                   {"addr": pointer(base + 0x120), "pointer": f"owner+{owner_offset}"},
                   {"addr": pointer(base + 0x128), "bytes": "00" * 8},
                   {"addr": pointer(base + 0x140), "guest": entry},
                   {"addr": pointer(base + 0x148), "bytes": "00" * 8},
                   {"addr": pointer(base + 0x150), "bytes": "00" * 4}]
        if thread == AUDIO_OUT_THREAD:
            memory += [{"addr": pointer(base + WAKEUP), "bytes": "00" * 8},
                       {"addr": pointer(base + STARTUP), "pointer": "startup-audio+8"}]
        else:
            memory += [{"addr": pointer(base + WAKEUP),
                        "pointer": "startup-audio+8" if wake_alias else "wakeup-mixer+8"},
                       {"addr": pointer(base + STARTUP),
                        "pointer": "startup-audio+8" if startup_alias else "startup-mixer+8"}]
    args = {"rdi": pointer(owner_offset + CALLBACK_BIAS),
            "rsi": "0xfedcba9876543210", "rdx": "0x8000000000000001",
            "rcx": "0xffffffffffffffff", "r8": "0xabcdef1234567890", "r9": "0x100000000"}
    assert not set(incoming or {}) - {"rsi", "rdx", "rcx", "r8", "r9"}
    args.update(incoming or {})
    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
              "args": args, "buffers": {
                  "owner": {"size": OWNER_BYTES, "bytes": fill * OWNER_BYTES},
                  "wakeup-mixer": {"size": 64, "bytes": "b6" * 64},
                  "startup-audio": {"size": 64, "bytes": "c7" * 64},
                  "startup-mixer": {"size": 64, "bytes": "d8" * 64}},
              "memory": memory,
              "stubs": [script(first), script(second), script(0)]}
    if stack:
        result["stack"] = list(stack)
    return result


def cases():
    """Cover the two status positions, guarded object addresses and owned effects."""
    paths = [(0, 0), (0, 33), (33, 0), (33, 33)]
    for offset in [0, 8, 16, 24, 32, 64]:
        for first, second in paths:
            yield case(f"path-{offset}-{first}-{second}", first, second, owner_offset=offset)
    for active in [1, 2, 0x7F, 0x80, 0xFF]:
        for neighbor in [0, 1, 0x7F, 0x80, 0xFF]:
            for first, second in [(0, 33), (33, 0), (33, 33)]:
                yield case(f"active-{active:02x}-neighbor-{neighbor:02x}-{first}-{second}",
                           first, second, active_byte=active, neighbor_byte=neighbor)
    for handle in [0, 1, 0x100000000, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF]:
        for first, second in paths:
            yield case(f"native-handle-{handle:016x}-{first}-{second}", first, second,
                       native_handle=handle)
    for fill in ["00", "01", "7f", "ff"]:
        for first, second in paths:
            yield case(f"guard-{fill}-{first}-{second}", first, second, fill=fill)
    for position in ["rsi", "rdx", "rcx", "r8", "r9"]:
        for index, value in enumerate(["0x0", "0x1", "0x100000000",
                                       "0x8000000000000000", "0xffffffffffffffff",
                                       "buf:owner+72"]):
            yield case(f"ignored-{position}-{index}", incoming={position: value})
    for index, values in enumerate([["0x0"] * 4, ["0xffffffffffffffff"] * 4,
                                    ["buf:owner+16", "0x100000000", "0x1", "0x8000000000000000"]]):
        for first, second in paths:
            yield case(f"ignored-stack-{index}-{first}-{second}", first, second, stack=values)
    for first, second in paths:
        yield case(f"owned-semaphore-alias-{first}-{second}", first, second,
                   wake_alias=True, startup_alias=True)


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
