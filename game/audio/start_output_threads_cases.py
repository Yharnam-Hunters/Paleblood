#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD output-thread startup cases. Usage: SCRIPT OUT_DIR.

Thread initialization is scripted; no actual threads or semaphores are created.
Every compared owner is readable. NULL owners fault in the original and are not
successful inputs. Callback effects below are partial owned write profiles.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x0111b140"
INITIALIZE = "0x01118420"
CALLBACK_BIAS = 0x38
SERVICE_CONTEXT = 0x60
AUDIO_OUT_THREAD = 0x6D0
MIXER_THREAD = 0x828
THREAD_VTABLE = "0x056ce930"
OWNER_BYTES = 0xB00
NORMAL_STATUSES = [0, 33]


def buffer_pointer(offset):
    """Name an owned address without duplicating the harness sandbox layout."""
    return f"buf:owner+{offset}"


def pointer_data(value):
    """Preserve a full opaque pointer value; pointed-to services are not run."""
    if isinstance(value, str) and value.startswith("buf:"):
        return {"pointer": value[4:]}
    if isinstance(value, str) and value.startswith("guest:"):
        return {"guest": value[6:]}
    return {"bytes": struct.pack("<Q", int(value, 0) & ((1 << 64) - 1)).hex()}


def entry(status, writes=()):
    """Observe only the initializer's proven logical argument positions."""
    assert status in NORMAL_STATUSES
    return {"address": INITIALIZE, "argc": 10, "argi": [0, 1, 2, 3, 9],
            "argu32": [4, 6, 8], "argu8": [7], "ret": hex(status),
            "writes": list(writes)}


def case(cid, first=0, second=0, owner_offset=16, service="buf:service-a+8",
         next_service=None, write_via_service=False, effects=False,
         fill="a5", incoming=None, stack=()):
    """Use a valid aligned owner and keep all presets and reached writes owned."""
    assert owner_offset in [0, 8, 16, 24, 32, 64]
    owner = buffer_pointer(owner_offset)
    memory = [{"addr": buffer_pointer(owner_offset + SERVICE_CONTEXT), **pointer_data(service)}]
    for thread in [AUDIO_OUT_THREAD, MIXER_THREAD]:
        base = owner_offset + thread
        memory.append({"addr": buffer_pointer(base), "guest": THREAD_VTABLE})
        for offset in [0x110, 0x120, 0x128, 0x130, 0x138, 0x140]:
            memory.append({"addr": buffer_pointer(base + offset), "bytes": "00" * 8})
        memory.append({"addr": buffer_pointer(base + 0x118), "bytes": "00"})
        memory.append({"addr": buffer_pointer(base + 0x150), "bytes": "00" * 4})
        memory.append({"addr": buffer_pointer(base + 0x148), "pointer": "service-c+8"})
    first_writes, second_writes = [], []
    if effects:
        for writes, callback, current in [(first_writes, "0x0111b910", service),
                                           (second_writes, "0x0111b920", next_service or service)]:
            writes += [{"arg": 0, "offset": 0x140, "guest": callback},
                       {"arg": 0, "offset": 0x120, **pointer_data(owner)},
                       {"arg": 0, "offset": 0x150, "bytes": "00" * 4},
                       {"arg": 0, "offset": 0x118, "bytes": "00"},
                       {"arg": 0, "offset": 0x148, **pointer_data(current)}]
    if next_service is not None:
        assert first == 0
        if write_via_service:
            assert service == owner
        first_writes.append({"arg": 9 if write_via_service else 3,
                             "offset": SERVICE_CONTEXT, **pointer_data(next_service)})
    args = {"rdi": buffer_pointer(owner_offset + CALLBACK_BIAS),
            "rsi": "0xfedcba9876543210", "rdx": "0x8000000000000001",
            "rcx": "0xffffffffffffffff", "r8": "0xabcdef1234567890", "r9": "0x100000000"}
    assert not set(incoming or {}) - {"rsi", "rdx", "rcx", "r8", "r9"}
    args.update(incoming or {})
    result = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
              "args": args, "buffers": {
                  "owner": {"size": OWNER_BYTES, "bytes": fill * OWNER_BYTES},
                  "service-a": {"size": 64, "bytes": "b6" * 64},
                  "service-b": {"size": 64, "bytes": "c7" * 64},
                  "service-c": {"size": 64, "bytes": "d8" * 64}},
              "memory": memory,
              "stubs": [entry(first, first_writes), entry(second, second_writes), entry(33)]}
    if stack:
        result["stack"] = list(stack)
    return result


def cases():
    """Cover both failure positions, pointer transport and success-only re-reads."""
    paths = [(0, 0), (0, 33), (33, 0), (33, 33)]
    for offset in [0, 8, 16, 24, 32, 64]:
        for first, second in paths:
            yield case(f"path-{offset}-{first}-{second}", first, second, owner_offset=offset)
    services = ["0x0", "0x1", "0x100000000", "0x8000000000000000",
                "0xffffffffffffffff", "0x0123456789abcdef", "buf:service-a",
                "buf:service-a+8", "buf:service-a+63", "buf:service-b+8", "buf:owner+16"]
    for index, service in enumerate(services):
        for first, second in paths:
            yield case(f"service-{index}-{first}-{second}", first, second, service=service)
    for index, next_service in enumerate(services):
        for second in NORMAL_STATUSES:
            yield case(f"reread-{index}-{second}", second=second,
                       next_service=next_service, effects=True)
    for second in NORMAL_STATUSES:
        yield case(f"service-alias-write-{second}", second=second,
                   service="buf:owner+16", next_service="buf:service-b+16",
                   write_via_service=True, effects=True)
    for position in ["rsi", "rdx", "rcx", "r8", "r9"]:
        for index, value in enumerate(["0x0", "0x1", "0x100000000",
                                       "0x8000000000000000", "0xffffffffffffffff",
                                       "buf:owner+72"]):
            yield case(f"ignored-{position}-{index}", incoming={position: value})
    for index, values in enumerate([["0x0"] * 4, ["0xffffffffffffffff"] * 4,
                                    ["buf:owner+16", "0x100000000", "0x1", "0x8000000000000000"]]):
        for first, second in paths:
            yield case(f"ignored-stack-{index}-{first}-{second}", first, second, stack=values)
    for fill in ["00", "01", "7f", "ff"]:
        for first, second in paths:
            yield case(f"guard-{fill}-{first}-{second}", first, second, fill=fill, effects=True)
    for first, second in paths:
        yield case(f"owned-prefix-effects-{first}-{second}", first, second, effects=True)


def main():
    """Write deterministic synthetic inputs with hyphen-only unique filenames."""
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
