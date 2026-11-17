#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD Thread startup cases. Usage: SCRIPT OUT_DIR.

Semaphore creation, native thread creation, waits, deletion and notification are
scripted boundaries. No OS thread executes. Both original name-copy helpers run
normally, including bounded copies and overlaps. Callback writes model visible
changes during calls; they are not recordings or claims about kernel behavior.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = "0x01118420"
CREATE_SEMAPHORE = "0x0111ad10"
CREATE_THREAD = "0x0111a9f0"
WAIT = "0x0111ada0"
DELETE = "0x0111ad80"
NOTIFY = "0x0111b920"
SERVICES = "0x056d6ae8"
VTABLE = "0x056ce930"
THREAD_BYTES = 0x158
OWNER_BYTES = 0x500
NAME = 9
HANDLE = 0x110
RUNNING = 0x118
ARGUMENT = 0x120
WAKEUP = 0x130
STARTUP = 0x138
ENTRY = 0x140
CONTEXT = 0x148
DELAY = 0x150
START_STORAGE = 0x200
WAKE_STORAGE = 0x240
OTHER_START_STORAGE = 0x280
OTHER_WAKE_STORAGE = 0x2C0
CONTEXT_STORAGE = 0x380
ARGUMENT_STORAGE = 0x400


def word(value):
    """Encode one full storage word, preserving its exact bits."""
    return struct.pack("<Q", value).hex()


def case(cid, priority=2, wake=1, notify=True, failure=None, change=None,
         owner_offset=32, fill=0xA5, name_length=19, name_null=False,
         name_alias=None, stack_bytes=0xC000, stack_upper=0,
         delay=0x89ABCDEF, unknown=0, result_upper=0,
         native_handle=0xFEDCBA9876543210, startup_null=False,
         wakeup_null=False, alias_handles=False, delete_results=(0, 0),
         entry="guest:0x011183a0", argument_null=False):
    """Build bounded objects and complete observing scripts for every call site."""
    assert failure in (None, "startup-create", "wakeup-create", "native-create", "wait")
    assert change in (None, "wakeup-create-startup", "wakeup-create-null-startup",
                      "native-create-startup", "native-create-null-startup",
                      "native-create-wakeup", "native-create-null-wakeup", "wait-startup", "wait-wakeup",
                      "wait-handle", "wait-context", "wait-running", "wait-name",
                      "wait-services", "delete-startup", "delete-wakeup",
                      "delete-running", "notify-handle", "notify-running")
    assert owner_offset in (0, 8, 16, 32, 64)
    assert owner_offset + ARGUMENT_STORAGE + 32 <= OWNER_BYTES
    assert 0 <= fill <= 255 and 0 <= wake <= 0xFFFFFFFFFFFFFFFF
    assert name_alias in (None, 0, 8, 9, 10)
    assert name_length is None or 0 <= name_length < 320
    assert result_upper & 0xFFFFFFFF == 0
    assert stack_upper & 0xFFFFFFFF == 0
    assert not (failure == "wakeup-create" and wake & 255 == 0)
    assert not (change == "wait-services" and (not notify or startup_null))

    def pointer(offset):
        return f"owner+{owner_offset + offset}"

    def location(offset):
        return "buf:" + pointer(offset)

    def preset(offset, **value):
        return {"addr": location(offset), **value}

    def write(arg, origin, field, **value):
        return {"arg": arg, "offset": field - origin, **value}

    def handle_value(offset):
        return {"bytes": word(0)} if offset is None else {"pointer": pointer(offset)}

    startup = None if startup_null else START_STORAGE
    wakeup = None if wakeup_null else START_STORAGE if alias_handles else WAKE_STORAGE
    services_alias = change == "wait-services"
    name = "0x0" if name_null else location(name_alias) if name_alias is not None else "buf:name+0"
    text = bytearray((index % 255) + 1 for index in range(320))
    if name_length is not None:
        text[name_length] = 0
    memory = [preset(0, guest=VTABLE),
              preset(HANDLE, bytes=word(0x1122334455667788)),
              preset(RUNNING, bytes="80"),
              preset(WAKEUP, pointer=pointer(OTHER_WAKE_STORAGE)),
              preset(STARTUP, pointer=pointer(OTHER_START_STORAGE)),
              preset(CONTEXT, pointer=pointer(CONTEXT_STORAGE + 16)),
              {"addr": SERVICES, "pointer": "services-a+0"},
              {"addr": "buf:services-a+24", "guest": NOTIFY} if notify
              else {"addr": "buf:services-a+24", "bytes": word(0)},
              {"addr": "buf:services-b+24", "bytes": word(0)}]

    def create_script(origin, target, failed, additional=()):
        writes = [] if failed else [{"arg": 0, "offset": 0, **handle_value(target)}]
        if services_alias and origin == STARTUP and not failed:
            writes = [{"arg": 0, "offset": 0, "guest": SERVICES}]
        return {"address": CREATE_SEMAPHORE, "argc": 1, "argi": [0],
                "argmem": [{"arg": 0, "offset": -origin, "size": THREAD_BYTES}],
                "ret": hex(result_upper | (33 if failed else 0)),
                "writes": writes + list(additional)}

    wake_changes = []
    if change in ("wakeup-create-startup", "wakeup-create-null-startup"):
        assert wake & 255 and failure != "wakeup-create"
        value = {"pointer": pointer(OTHER_START_STORAGE)} if change == "wakeup-create-startup" else {"bytes": word(0)}
        wake_changes.append(write(0, WAKEUP, STARTUP, **value))
    creates = [create_script(STARTUP, startup, failure == "startup-create"),
               create_script(WAKEUP, wakeup, failure == "wakeup-create", wake_changes),
               {"address": CREATE_SEMAPHORE, "argc": 1, "argi": [0], "ret": "0x21"}]

    native_writes = []
    if failure != "native-create":
        native_writes.append({"arg": 6, "offset": 0, "bytes": word(native_handle)})
    if change in ("native-create-startup", "native-create-null-startup"):
        value = {"pointer": pointer(OTHER_START_STORAGE)} if change == "native-create-startup" else {"bytes": word(0)}
        native_writes.append(write(2, 0, STARTUP, **value))
    if change in ("native-create-wakeup", "native-create-null-wakeup"):
        value = {"pointer": pointer(OTHER_WAKE_STORAGE)} if change == "native-create-wakeup" else {"bytes": word(0)}
        native_writes.append(write(2, 0, WAKEUP, **value))
    # The lower helper consumes DWORD priority and stack size. Its fifth argument
    # is unused, so unused register bits are excluded from the observation contract.
    native = {"address": CREATE_THREAD, "argc": 7, "argi": [0, 1, 2, 6],
              "argu32": [3, 5], "ret": hex(result_upper | (33 if failure == "native-create" else 0)),
              "argmem": [{"arg": 2, "offset": 0, "size": THREAD_BYTES}],
              "writes": native_writes}

    wait_target = OTHER_START_STORAGE if change in ("wakeup-create-startup", "native-create-startup") else startup
    if change in ("wakeup-create-null-startup", "native-create-null-startup"):
        wait_target = None
    wait_writes = []
    if change in ("wait-startup", "wait-wakeup", "wait-handle", "wait-context", "wait-running", "wait-name"):
        assert wait_target is not None
        fields = {"wait-startup": (STARTUP, {"pointer": pointer(OTHER_START_STORAGE)}),
                  "wait-wakeup": (WAKEUP, {"pointer": pointer(OTHER_WAKE_STORAGE)}),
                  "wait-handle": (HANDLE, {"bytes": word(0x8000000100000000)}),
                  "wait-context": (CONTEXT, {"pointer": pointer(CONTEXT_STORAGE + 16)}),
                  "wait-running": (RUNNING, {"bytes": "fe"}),
                  "wait-name": (NAME, {"bytes": "fe8100"})}
        field, value = fields[change]
        wait_writes.append(write(0, wait_target, field, **value))
    if services_alias:
        wait_writes.append({"arg": 0, "offset": 0, "pointer": "services-b+0"})
    wait_script = {"address": WAIT, "argc": 1, "argi": [0],
                   "ret": hex(result_upper | (33 if failure == "wait" else 0)),
                   "writes": wait_writes}

    cleanup_handles = []
    if wake & 255 and failure != "wakeup-create" and wakeup is not None:
        cleanup_handles.append(wakeup)
    if startup is not None:
        cleanup_handles.append(startup)
    deletes = []
    for index, handle in enumerate(cleanup_handles):
        writes = []
        if index == 0 and change in ("delete-startup", "delete-wakeup", "delete-running"):
            field, value = {"delete-startup": (STARTUP, {"pointer": pointer(OTHER_START_STORAGE)}),
                            "delete-wakeup": (WAKEUP, {"pointer": pointer(OTHER_WAKE_STORAGE)}),
                            "delete-running": (RUNNING, {"bytes": "81"})}[change]
            writes.append(write(0, handle, field, **value))
        deletes.append({"address": DELETE, "argc": 1, "argi": [0],
                        "ret": hex(delete_results[min(index, 1)]), "writes": writes})
    deletes.append({"address": DELETE, "argc": 1, "argi": [0], "ret": "0x21"})

    notification_writes = []
    if change == "notify-handle":
        notification_writes.append(write(0, CONTEXT_STORAGE, HANDLE, bytes=word(0x100000001)))
    if change == "notify-running":
        notification_writes.append(write(0, CONTEXT_STORAGE, RUNNING, bytes="7f"))
    notification = {"address": NOTIFY, "argc": 4, "argi": [0, 2, 3], "argu32": [1],
                    "argmem": [{"arg": 0, "offset": -CONTEXT_STORAGE, "size": THREAD_BYTES}],
                    "ret": "0xffffffffffffffff", "writes": notification_writes}
    row = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
           "args": {"rdi": location(0), "rsi": name, "rdx": entry,
                    "rcx": "0x0" if argument_null else location(ARGUMENT_STORAGE),
                    "r8": hex(priority & 0xFFFFFFFFFFFFFFFF), "r9": hex(unknown)},
           "stack": [hex(stack_upper | (stack_bytes & 0xFFFFFFFF)), hex(wake),
                     hex(stack_upper | (delay & 0xFFFFFFFF)), location(CONTEXT_STORAGE)],
           "buffers": {"owner": {"size": OWNER_BYTES, "bytes": bytes([fill] * OWNER_BYTES).hex()},
                       "name": {"size": len(text), "bytes": text.hex()},
                       "services-a": {"size": 64, "bytes": "b6" * 64},
                       "services-b": {"size": 64, "bytes": "c7" * 64}},
           "memory": memory, "imports": [],
           "stubs": [*creates, native, wait_script, *deletes, notification]}
    return row


def cases():
    """Vary each startup gate, saved handle, re-read, width and copy boundary."""
    for priority in range(-2, 4):
        for wake in (0, 1):
            for notify in (False, True):
                for null_name in (False, True):
                    yield case(f"success-p{priority}-w{wake}-n{int(notify)}-null{int(null_name)}",
                               priority=priority, wake=wake, notify=notify, name_null=null_name)
    for priority in (-0x80000000, -4, -3, 4, 5, 0x7FFFFFFF, 0x100000000, 0x100000003):
        for wake in (0, 1):
            yield case(f"priority-{priority & 0xFFFFFFFFFFFFFFFF:016x}-w{wake}", priority=priority, wake=wake)
    for failure in ("startup-create", "wakeup-create", "native-create", "wait"):
        for wake in (0, 1):
            if failure == "wakeup-create" and not wake:
                continue
            for null_name in (False, True):
                yield case(f"error-{failure}-w{wake}-null{int(null_name)}", failure=failure,
                           wake=wake, name_null=null_name)
    for wake in (0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x100000000, 0x100000001, 0xFFFFFFFFFFFFFFFF):
        for failure in (None, "wait"):
            yield case(f"wake-byte-{wake:016x}-{failure or 'success'}", wake=wake, failure=failure)
    for length in (0, 1, 5, 254, 255, 256, 319, None):
        yield case(f"name-boundary-{length if length is not None else 'unterminated'}", name_length=length)
    for offset in (0, 8, 16, 32, 64):
        for fill in (0, 1, 0x7F, 0x80, 0xFF):
            yield case(f"object-{offset}-fill-{fill:02x}", owner_offset=offset, fill=fill)
    for offset in (0, 8, 9, 10):
        yield case(f"name-overlap-{offset}", name_alias=offset)
    for change in ("wakeup-create-startup", "wakeup-create-null-startup",
                   "native-create-startup", "native-create-null-startup",
                   "native-create-wakeup", "native-create-null-wakeup",
                   "wait-startup", "wait-wakeup", "wait-handle", "wait-context", "wait-running",
                   "wait-name", "wait-services"):
        for failure in (None, "wait"):
            yield case(f"change-{change}-{failure or 'success'}", change=change, failure=failure)
    for change in ("delete-startup", "delete-wakeup", "delete-running"):
        for failure in ("native-create", "wait"):
            yield case(f"change-{change}-{failure}", change=change, failure=failure,
                       delete_results=(33, 33))
    for change in ("notify-handle", "notify-running"):
        yield case("change-" + change, change=change)
    for startup_null in (False, True):
        for wakeup_null in (False, True):
            for failure in (None, "native-create", "wait"):
                yield case(f"null-handles-{int(startup_null)}-{int(wakeup_null)}-{failure or 'success'}",
                           startup_null=startup_null, wakeup_null=wakeup_null, failure=failure)
    for failure in (None, "native-create", "wait"):
        yield case(f"aliased-handles-{failure or 'success'}", alias_handles=True, failure=failure)
    yield case("cached-null-startup-changed-before-wait", startup_null=True,
               change="native-create-startup", failure="wait")
    yield case("cached-null-startup-changed-during-wakeup", startup_null=True,
               change="wakeup-create-startup", failure="wait")
    yield case("cached-null-wakeup-changed-before-error", wakeup_null=True,
               change="native-create-wakeup", failure="native-create")
    for first, second in ((33, 0), (0, 33), (33, 33)):
        for failure in ("native-create", "wait"):
            yield case(f"ignored-delete-errors-{first}-{second}-{failure}", failure=failure,
                       delete_results=(first, second))
        yield case(f"priority-error-ignores-delete-{first}-{second}", priority=4,
                   delete_results=(first, second))
    for failure in ("startup-create", "wakeup-create"):
        yield case("priority-check-after-" + failure, priority=4, failure=failure)
    for value in (0, 1, 0x100000000, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF):
        yield case(f"native-handle-{value:016x}", native_handle=value)
        yield case(f"unused-parameter-{value:016x}", unknown=value)
    for value in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
        yield case(f"stack-size-{value:08x}", stack_bytes=value)
        yield case(f"callback-delay-{value:08x}", delay=value)
    for upper in (0x100000000, 0x8000000000000000, 0xFFFFFFFF00000000):
        for failure in (None, "startup-create", "wakeup-create", "native-create", "wait"):
            yield case(f"result-upper-{upper:016x}-{failure or 'success'}", result_upper=upper, failure=failure)
        yield case(f"stack-slot-upper-{upper:016x}", stack_upper=upper)
    yield case("null-entry-and-argument", entry="0x0", argument_null=True)


def main():
    """Write deterministic fixture dictionaries with unique, explicit case IDs."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len(generated) == len({row["id"] for row in generated})
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in generated:
        (output / (fixture["id"] + ".json")).write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
