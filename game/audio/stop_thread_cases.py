#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD Thread-stop boundaries. Usage: SCRIPT OUT_DIR.

Semaphore helpers are scripted only with their observed DWORD results, 0 and 33.
Owned handles do not establish OS semaphore validity. Writes during the scripted
boundaries model changes visible after a call, not recorded kernel behavior.
Notification and release are also scripted; the read-only source string and the
original zero-return handle helper execute without fixture patches.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x011185a0"
SIGNAL = "0x0111adc0"
WAIT = "0x0111ada0"
DELETE = "0x0111ad80"
NOTIFY = "0x0111b920"  # Owned scripted callback; no claim about this entry's body.
RELEASE = "0x011ab3a0"
SERVICES = "0x056d6ae8"
VTABLE = "0x056ce930"
NAME = 9
HANDLE = 0x110
ACTIVE = 0x118
BLOCK = 0x128
WAKEUP = 0x130
STARTUP = 0x138
CONTEXT = 0x148
WAKE_STORAGE = 0x180
START_STORAGE = 0x1A0
OTHER_WAKE_STORAGE = 0x1C0
OTHER_START_STORAGE = 0x1E0
BLOCK_STORAGE = 0x220
OTHER_BLOCK_STORAGE = 0x240
CONTEXT_STORAGE = 0x260
OWNER_BYTES = 0x400


def word(value):
    """Encode a full, little-endian storage word."""
    return struct.pack("<Q", value).hex()


def case(cid, active=1, neighbor=0xA5, wake=True, notify=True, block=True,
         failure=None, change=None, owner_offset=32, handle=0xFEDCBA9876543210,
         fill=0xA5, incoming=None, stack=(), startup_null=False, alias_handles=False):
    """Create bounded objects and ordered helper scripts, including observing stubs."""
    assert 0 <= active <= 255 and 0 <= neighbor <= 255
    assert failure in (None, "signal", "wait", "wake-delete", "startup-delete")
    assert change in (None, "signal-startup", "wait-add-wake", "wait-remove-wake",
                      "wait-change-wake", "wake-delete-startup", "wake-delete-null-startup",
                      "wake-delete-write-own", "startup-delete-write-own", "notify-handle",
                      "startup-delete-handle", "startup-delete-context", "startup-delete-services",
                      "notify-running",
                      "notify-add-block", "notify-remove-block", "notify-change-block",
                      "notify-services", "release-write-block", "release-write-handle")
    assert 0 <= owner_offset <= 64

    def pointer(offset):
        return f"owner+{owner_offset + offset}"

    def location(offset):
        return "buf:" + pointer(offset)

    def preset(offset, **value):
        return {"addr": location(offset), **value}

    def write(arg, origin, field, **value):
        return {"arg": arg, "offset": field - origin, **value}

    initial_wake = WAKE_STORAGE if wake else None
    initial_start = (WAKE_STORAGE if alias_handles else START_STORAGE) if not startup_null else None
    wait_target = OTHER_START_STORAGE if change == "signal-startup" and wake else initial_start
    delete_wake = initial_wake
    if change == "wait-add-wake":
        assert not wake
        delete_wake = OTHER_WAKE_STORAGE
    if change == "wait-remove-wake":
        assert wake
        delete_wake = None
    if change == "wait-change-wake":
        assert wake
        delete_wake = OTHER_WAKE_STORAGE
    delete_start = wait_target
    if change == "wake-delete-startup":
        assert delete_wake is not None
        delete_start = OTHER_START_STORAGE
    if change == "wake-delete-null-startup":
        assert delete_wake is not None and failure == "startup-delete"
        delete_start = None
    if startup_null:
        assert failure == "wait"

    memory = [preset(0, guest=VTABLE),
              preset(NAME, bytes=(b"synthetic Thread stop\0").hex()),
              preset(HANDLE, bytes=word(handle)),
              preset(ACTIVE, bytes=bytes([active, neighbor]).hex()),
              preset(BLOCK, pointer=pointer(BLOCK_STORAGE)) if block else preset(BLOCK, bytes=word(0)),
              preset(WAKEUP, pointer=pointer(initial_wake)) if wake else preset(WAKEUP, bytes=word(0)),
              preset(STARTUP, pointer=pointer(initial_start)) if initial_start is not None
              else preset(STARTUP, bytes=word(0)),
              preset(CONTEXT, guest=SERVICES) if change == "notify-services"
              else preset(CONTEXT, pointer=pointer(CONTEXT_STORAGE)),
              {"addr": SERVICES, "pointer": "services-a+0"},
              {"addr": "buf:services-a+8", "pointer": "pool-a+8"},
              {"addr": "buf:services-b+8", "pointer": "pool-b+16"},
              {"addr": "buf:services-a+24", "guest": NOTIFY} if notify
              else {"addr": "buf:services-a+24", "bytes": word(0)},
              {"addr": "buf:services-b+24", "bytes": word(0)}]

    def semaphore(address, target, failed, writes=()):
        entry = {"address": address, "argc": 1, "argi": [0],
                 "ret": "0x21" if failed else "0x0", "writes": list(writes)}
        if target is not None:
            entry["argmem"] = [{"arg": 0, "offset": field - target, "size": size}
                               for field, size in [(ACTIVE, 1), (HANDLE, 8), (BLOCK, 24)]]
        return entry

    signal_writes = []
    if change == "signal-startup":
        signal_writes.append(write(0, initial_wake, STARTUP, pointer=pointer(OTHER_START_STORAGE)))
    signal = semaphore(SIGNAL, initial_wake, failure == "signal", signal_writes)
    wait_writes = []
    if change in ("wait-add-wake", "wait-change-wake"):
        wait_writes.append(write(0, wait_target, WAKEUP, pointer=pointer(OTHER_WAKE_STORAGE)))
    if change == "wait-remove-wake":
        wait_writes.append(write(0, wait_target, WAKEUP, bytes=word(0)))
    wait = semaphore(WAIT, wait_target, failure == "wait", wait_writes)
    wake_writes = []
    if change == "wake-delete-startup":
        wake_writes.append(write(0, delete_wake, STARTUP, pointer=pointer(OTHER_START_STORAGE)))
    if change == "wake-delete-null-startup":
        wake_writes.append(write(0, delete_wake, STARTUP, bytes=word(0)))
    if change == "wake-delete-write-own":
        wake_writes.append(write(0, delete_wake, WAKEUP, pointer=pointer(OTHER_WAKE_STORAGE)))
    wake_delete = semaphore(DELETE, delete_wake, failure == "wake-delete", wake_writes)
    start_writes = []
    if change == "startup-delete-write-own":
        start_writes.append(write(0, delete_start, STARTUP, pointer=pointer(OTHER_START_STORAGE)))
    if change == "startup-delete-handle":
        start_writes.append(write(0, delete_start, HANDLE, bytes=word(0x8000000100000000)))
    if change == "startup-delete-context":
        start_writes.append(write(0, delete_start, CONTEXT, pointer=pointer(CONTEXT_STORAGE + 8)))
    if change == "startup-delete-services":
        # A deliberately synthetic handle alias permits a bounded write to the
        # writable global slot. No original context bytes are read or patched.
        memory.append(preset(STARTUP, guest=SERVICES))
        wait.pop("argmem", None)
        start_writes.append({"arg": 0, "offset": 0, "pointer": "services-b+0"})
    start_delete = semaphore(DELETE, None if change == "startup-delete-services" else delete_start,
                             failure == "startup-delete", start_writes)
    # Leave an observer for the optional-delete branch even when the original skips it.
    deletes = [wake_delete, start_delete] if delete_wake is not None else [start_delete, wake_delete]

    notify_writes = []
    if change == "notify-handle":
        notify_writes.append(write(3, NAME, HANDLE, bytes=word(0x123456789ABCDEF0)))
    if change == "notify-running":
        notify_writes.append(write(3, NAME, ACTIVE, bytes="80"))
    if change in ("notify-add-block", "notify-change-block"):
        assert notify
        notify_writes.append(write(3, NAME, BLOCK, pointer=pointer(OTHER_BLOCK_STORAGE)))
    if change == "notify-remove-block":
        notify_writes.append(write(3, NAME, BLOCK, bytes=word(0)))
    if change == "notify-services":
        assert notify
        notify_writes.append({"arg": 0, "offset": 0, "pointer": "services-b+0"})
    notification = {"address": NOTIFY, "argc": 4, "argi": [0, 2, 3], "argu32": [1],
                    "argmem": [{"arg": 3, "offset": HANDLE - NAME, "size": 16},
                               {"arg": 3, "offset": BLOCK - NAME, "size": 24}],
                    "ret": "0xdeadbeef", "writes": notify_writes}
    release_block = OTHER_BLOCK_STORAGE if change in ("notify-add-block", "notify-change-block") else BLOCK_STORAGE
    release_writes = []
    if change == "release-write-block":
        release_writes.append(write(1, release_block, BLOCK, pointer=pointer(OTHER_BLOCK_STORAGE)))
    if change == "release-write-handle":
        release_writes.append(write(1, release_block, HANDLE, bytes=word(0x100000001)))
    release = {"address": RELEASE, "argc": 4, "argi": [0, 1, 2], "argu32": [3],
               "argmem": [{"arg": 1, "offset": HANDLE - release_block, "size": 16},
                          {"arg": 1, "offset": BLOCK - release_block, "size": 8}],
               "ret": "0x8000000000000021", "writes": release_writes}
    args = {"rdi": location(0), "rsi": "0xffffffffffffffff", "rdx": "0x8000000000000001",
            "rcx": "0xfedcba9876543210", "r8": "0x100000000", "r9": "0x1"}
    assert not set(incoming or {}) - {"rsi", "rdx", "rcx", "r8", "r9", "rax"}
    args.update(incoming or {})
    row = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32", "args": args,
           "buffers": {"owner": {"size": OWNER_BYTES, "bytes": bytes([fill] * OWNER_BYTES).hex()},
                       **{name: {"size": 64, "bytes": bytes([seed] * 64).hex()}
                          for name, seed in [("services-a", 0xB6), ("services-b", 0xC7),
                                             ("pool-a", 0xD8), ("pool-b", 0xE9)]}},
           "memory": memory, "imports": [],
           "stubs": [signal, wait, *deletes, notification, release]}
    if stack:
        row["stack"] = list(stack)
    return row


def cases():
    """Vary each independent gate, error boundary, re-read and post-call clear."""
    for offset in (0, 8, 16, 32, 64):
        for wake in (False, True):
            for notify in (False, True):
                for block in (False, True):
                    yield case(f"success-{offset}-{int(wake)}-{int(notify)}-{int(block)}",
                               owner_offset=offset, wake=wake, notify=notify, block=block)
    for active in (0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x7F, 0x80, 0xFF):
        for neighbor in (0, 1, 0x80, 0xFF):
            yield case(f"active-{active:02x}-neighbor-{neighbor:02x}", active=active, neighbor=neighbor)
    for stage in ("signal", "wait", "wake-delete", "startup-delete"):
        yield case("error-" + stage, failure=stage)
        if stage != "signal" and stage != "wake-delete":
            yield case("no-wake-error-" + stage, wake=False, failure=stage)
    for change in ("signal-startup", "wait-remove-wake", "wait-change-wake", "wake-delete-startup",
                   "wake-delete-write-own", "startup-delete-write-own", "notify-handle",
                   "startup-delete-handle", "startup-delete-context", "startup-delete-services",
                   "notify-running",
                   "notify-remove-block", "notify-change-block", "notify-services",
                   "release-write-block", "release-write-handle"):
        yield case(change, change=change)
    yield case("wait-add-wake", wake=False, change="wait-add-wake")
    yield case("notify-add-block", block=False, change="notify-add-block")
    yield case("null-startup-wait-error", startup_null=True, failure="wait")
    yield case("delete-fresh-null-startup-error", change="wake-delete-null-startup", failure="startup-delete")
    yield case("wake-delete-error-preserves-write", change="wake-delete-write-own", failure="wake-delete")
    yield case("startup-delete-error-preserves-write", change="startup-delete-write-own", failure="startup-delete")
    yield case("signal-error-preserves-startup-change", change="signal-startup", failure="signal")
    yield case("wait-error-preserves-wake-change", change="wait-change-wake", failure="wait")
    yield case("aliased-owned-handles", alias_handles=True)
    yield case("aliased-owned-handles-wait-error", alias_handles=True, failure="wait")
    for handle in (0, 1, 0x100000000, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF):
        yield case(f"handle-{handle:016x}", handle=handle)
    for fill in (0, 1, 0x7F, 0xFF):
        yield case(f"owned-fill-{fill:02x}", fill=fill)
    for reg in ("rsi", "rdx", "rcx", "r8", "r9", "rax"):
        for index, value in enumerate(("0x0", "0x100000000", "0xffffffffffffffff", "buf:owner+72")):
            yield case(f"ignored-{reg}-{index}", incoming={reg: value})
    for index, stack in enumerate((("0x0",) * 4, ("0xffffffffffffffff",) * 4,
                                   ("buf:owner+72", "0x1", "0x100000000", "0x8000000000000000"))):
        yield case(f"ignored-stack-{index}", stack=stack)


def main():
    """Write deterministic, unique synthetic fixture inputs."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert generated and len(generated) == len({row["id"] for row in generated})
    assert all("_" not in row["id"] for row in generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
