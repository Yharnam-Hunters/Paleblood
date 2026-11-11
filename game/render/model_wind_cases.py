#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for render_update_model_wind (0x02713870); not game recordings.

Usage: model_wind_cases.py OUT_DIR. Generate captures outside the repository.

Every callee is scripted: the receiver storage getter and resize, the wind source lookup, the
fractal noise and the sources' virtual methods (scripted stubs, or for some sources the game's
own one-instruction getters, so vector results carry all four lanes). The generator only models
which calls happen (the skip rules and which lookups return an entry); it never computes the
function's results.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import struct
import sys

ADDRESS = "0x02713870"
MASK64 = (1 << 64) - 1
PAGE = 4096

RESOLVE, GET_STORAGE, RESIZE, NOISE = "0x01314320", "0x0132c720", "0x0132c6c0", "0x02a85e30"
SYSTEM_SLOT, REGISTRY_SLOT, DEVICE_SLOT = "0x05940e00", "0x058b0bb0", "0x059406c8"
HOLD_FLAG, EMPTY_LIST, FLIPPER_SLOT, PATCH_CELL = "0x05940df9", "0x05ac4220", "0x059404f8", "0x0575a908"

# Fake method addresses (scripted stubs, one per virtual method).
METHODS = ["query_0x20", "jitter_angles", "base_speed", "gust_speeds", "noise_time", "transform", "cycle", "kind",
           "base_force", "point_source", "use_alternate_extra", "alternate_extra", "force_scale", "tint", "extra"]
SLOT = {"query_0x20": 0x20, "jitter_angles": 0xE0, "base_speed": 0xE8, "gust_speeds": 0xF0, "noise_time": 0xF8,
        "transform": 0x100, "cycle": 0x108, "kind": 0x110, "base_force": 0x120, "point_source": 0x128,
        "use_alternate_extra": 0x140, "alternate_extra": 0x148, "force_scale": 0x170, "tint": 0x178, "extra": 0x1A0}
FAKE = {name: f"0x{0x00500300 + 0x20 * i:08x}" for i, name in enumerate(METHODS)}
# The game's own getters: vmovaps xmm0, [rdi+offset]; ret, and vmovss xmm0, [rdi+offset]; ret.
VECTOR_GETTER = {"base_force": ("0x02c80470", 0x20), "tint": ("0x02c80480", 0x10), "query_0x20": ("0x02c80480", 0x10)}
FLOAT_GETTER = {"base_speed": ("0x02be60e0", 0x34), "noise_time": ("0x02be60f0", 0x38), "extra": ("0x028ce080", 0x78),
                "alternate_extra": ("0x02a77da0", 0x70), "force_scale": ("0x02a77db0", 0x74)}

SYSTEM_BYTES, RECEIVER_BYTES, STORAGE_BYTES, OUT_BYTES = 0x60, 0x178, 0xC40, 0x620
ENTRY_BYTES, OBJECT_BYTES, VTABLE_BYTES = 0x50, 0x80, 0x1B0
INFLUENCE = 0xC0
STRONGEST_OUT, OTHER_OUT = 0x10, 0x320
STRONGEST_KEPT, OTHER_KEPT, STRONGEST_RAMP, OTHER_RAMP = 0x10, 0x310, 0x610, 0x620
STRONGEST_TARGET, OTHER_TARGET, SETTLE_CALLS = 0x630, 0x930, 0xC30
INVALID = MASK64


def f32(value) -> int:
    """Bits of a binary32 value (an int is taken as raw bits)."""
    if isinstance(value, int):
        return value & 0xFFFFFFFF
    return struct.unpack("<I", struct.pack("<f", value))[0]


def f32hex(value) -> str:
    return struct.pack("<I", f32(value)).hex()


def vec(*lanes) -> str:
    return "".join(f32hex(v) for v in lanes)


def qword(value) -> str:
    return struct.pack("<Q", value & MASK64).hex()


def dword(value) -> str:
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


QNAN_A, QNAN_B, SNAN = 0x7FC12345, 0xFFC54321, 0x7F800321
INF, NEG_INF = 0x7F800000, 0xFF800000


EPSILON = struct.unpack("<f", bytes.fromhex("00000034"))[0]


def as_float(value) -> float:
    return struct.unpack("<f", struct.pack("<I", f32(value)))[0]


def round32(value: float) -> float:
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def gust_margin(gust_base, base_speed, scale) -> float:
    """|(gust base - base speed) x scale| in binary32 (NaN compares false)."""
    difference = round32(as_float(gust_base) - as_float(base_speed))
    return abs(round32(difference * as_float(scale)))


def buffer_offsets(buffers: dict) -> dict:
    """Where the harness places each buffer relative to the first (sorted names, guard pages)."""
    out, offset = {}, 0
    for name in sorted(buffers):
        out[name] = offset
        offset += (max(1, -(-buffers[name]["size"] // PAGE)) + 1) * PAGE
    return out


class Source:
    """One attached wind source and what its lookups and methods report."""

    def __init__(self, handle, **kw):
        self.handle = handle
        self.state = kw.get("state", 0)
        self.lookups = kw.get("lookups", {})          # call number (1..17) -> None for "no entry"
        self.no_object_at = kw.get("no_object_at", set())   # lookups whose entry has no object
        self.getters = kw.get("getters", False)
        self.kind = kw.get("kind", 0)
        self.point = kw.get("point", 0)
        self.orientation = kw.get("orientation", [(1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0),
                                                  (2.0, 3.0, 4.0, 1.0)])
        self.jitter = kw.get("jitter", ((-0.25, 0.5, -1.0, 7.0), (0.75, 1.5, 2.0, -3.0)))
        self.base_speed = kw.get("base_speed", 1.5)
        self.gust = kw.get("gust", (2.5, 0.75))
        self.noise_time = kw.get("noise_time", 3.25)
        self.cycle = kw.get("cycle", (9.0, 0.375))
        self.extra = kw.get("extra", 0.625)
        self.base_force = kw.get("base_force", (0.125, -0.5, 0.25, 9.0))
        self.use_alternate = kw.get("use_alternate", 0)
        self.alternate = kw.get("alternate", -2.25)
        self.scale = kw.get("scale", 1.25)
        self.tint = kw.get("tint", (0.5, 0.25, 2.0, 4.0))
        self.query = kw.get("query", (11.0, 12.0, 13.0, 14.0))
        self.transform = kw.get("transform", [(0.0, 1.0, 0.0, 0.5), (-1.0, 0.0, 0.0, 0.25), (0.0, 0.0, 1.0, -0.5),
                                              (10.0, 20.0, 30.0, 1.0)])
        self.noise = kw.get("noise", (0.375, -0.0625))


class Case:
    def __init__(self, cid, **kw):
        self.cid = cid
        self.kw = kw
        self.buffers = {}
        self.memory = []
        self.queues = {}
        self.carried_gust = (0.0, 0.0)   # gust speeds a lookup without an entry leaves in place

    def buffer(self, name, size, fill="00"):
        self.buffers[name] = {"size": size, "bytes": fill * size}

    def put(self, name, offset, hexbytes):
        self.memory.append({"addr": f"buf:{name}+0x{offset:x}", "bytes": hexbytes})

    def pointer(self, name, offset, target):
        self.memory.append({"addr": f"buf:{name}+0x{offset:x}", "pointer": target})

    def stub(self, address, entry):
        self.queues.setdefault(address, []).append({"address": address, **entry})


def influence_bytes(life=1.0, handle=INVALID, kind=0, flag=0, point=0, seed=0, force=None, time=None) -> dict:
    """An influence's fields as offset -> hex, with distinct values from `seed`."""
    s = seed * 0.0625
    force = force or (1.0 + s, -2.0, 3.0)
    return {0x0: dword(kind), 0x10: vec(1.0 + s, 0.5, 0.0, 0.0), 0x20: vec(0.0, 1.0, s, 0.0),
            0x30: vec(0.0, 0.0, 1.0, s), 0x40: vec(4.0, 5.0 + s, 6.0, 1.0), 0x50: vec(0.25 + s, 1.0, 0.5, 0.0),
            0x60: vec(*force, 10.0 + s if time is None else time), 0x70: vec(0.5 + s, 0.25, 0.75, 0.0),
            0x80: vec(4.0, 5.0, 6.0, 0.125 + s), 0x90: vec(1.0, 2.0, 3.0, 4.0 + s),
            0xA0: f32hex(life) + "c1c2c3c4", 0xA8: qword(handle), 0xB0: f"{flag:02x}d1d2d3" + dword(point)}


def build(cid, sources=(), handle=0, capacity=4, receiver=True, disabled=0, settling=0, reset=0, hold=0,
          storage="kept", storage_null_first=False, storage_null_after_resize=False, handle_after_get=None,
          handle_after_resize=None, kept=None, active=(1, 1), settle_calls=0, ramps=(0.0, 0.0, 0.0, 0.0),
          other_ramps=(0.0, 0.0, 0.0, 0.0), out_fill="5a", out_counts=None, mode=1, frame_seconds=1.0 / 60.0,
          patch_cell=None, empty_list=True, storage_fill="00", receiver_index=None):
    c = Case(cid)
    c.buffer("handle", 8)
    c.buffer("out", OUT_BYTES, out_fill)
    c.buffer("system", SYSTEM_BYTES)
    c.buffer("table", 8 * max(1, capacity))
    c.buffer("receiver", RECEIVER_BYTES, "e7")
    c.buffer("registry", 0x110, "3c")
    c.buffer("device", 0x104)
    c.buffer("flipper", 0x2C8)
    c.buffer("handles", 8 * max(1, len(sources)))
    if storage:
        c.buffer("storage", STORAGE_BYTES, storage_fill)
    c.put("handle", 0, qword(handle))
    c.memory.append({"addr": SYSTEM_SLOT, "pointer": "system"})
    c.pointer("system", 0x8, "table")
    c.put("system", 0x48, dword(capacity))
    c.put("system", 0x4C, f"{reset:02x}{settling:02x}")
    c.put("system", 0x54, f"{disabled:02x}")
    index = (handle & 0xFFFFFFFF) if receiver_index is None else receiver_index
    if receiver and index < capacity:
        c.pointer("table", 8 * index, "receiver")
    c.memory.append({"addr": REGISTRY_SLOT, "pointer": "registry"})
    c.memory.append({"addr": DEVICE_SLOT, "pointer": "device"})
    c.put("device", 0x100, dword(mode))
    c.memory.append({"addr": FLIPPER_SLOT, "pointer": "flipper"})
    c.put("flipper", 0x264, f32hex(frame_seconds))
    c.memory.append({"addr": HOLD_FLAG, "bytes": f"{hold:02x}"})
    if patch_cell is not None:
        c.memory.append({"addr": PATCH_CELL, "bytes": f32hex(patch_cell)})
    # The image's empty list holds unrelocated data before the game sets it up: give it a value.
    list_begin, list_end = f"0x{int(EMPTY_LIST, 16) + 8:08x}", f"0x{int(EMPTY_LIST, 16) + 16:08x}"
    if empty_list == "sources":
        c.memory.append({"addr": list_begin, "pointer": "handles"})
        c.memory.append({"addr": list_end, "pointer": f"handles+0x{8 * len(sources):x}"})
    elif empty_list:
        c.memory.append({"addr": list_begin, "bytes": qword(0) + qword(0)})
    c.pointer("receiver", 0x130, "handles")
    c.pointer("receiver", 0x138, f"handles+0x{8 * len(sources):x}")
    for i, source in enumerate(sources):
        c.put("handles", 8 * i, qword(source.handle))
    if out_counts:
        c.put("out", 0x8, dword(out_counts[0]))
        c.put("out", 0x310, dword(out_counts[1]))

    if storage:
        c.put("storage", 0, f"{active[0]:02x}{active[1]:02x}")
        c.put("storage", SETTLE_CALLS, dword(settle_calls))
        for i in range(4):
            c.put("storage", STRONGEST_RAMP + 4 * i, f32hex(ramps[i]))
            c.put("storage", OTHER_RAMP + 4 * i, f32hex(other_ramps[i]))
        for (list_base, target_base), slots in zip(((STRONGEST_KEPT, STRONGEST_TARGET), (OTHER_KEPT, OTHER_TARGET)),
                                                   kept or ([], [])):
            for i, slot in enumerate(slots):
                if slot is None:
                    continue
                for offset, data in influence_bytes(**slot).items():
                    c.put("storage", list_base + INFLUENCE * i + offset, data)
                target = dict(slot, seed=slot.get("seed", 0) + 7)
                for offset, data in influence_bytes(**target).items():
                    c.put("storage", target_base + INFLUENCE * i + offset, data)

    # The receiver lookup: the storage getter (and resize on a miss).
    def handle_write(after):
        offsets = buffer_offsets(c.buffers)
        delta = offsets["handle"] - offsets["receiver"]
        return [{"arg": 0, "offset": delta, "bytes": qword(after)}]

    valid = handle != INVALID and (handle & 0xFFFFFFFF) < capacity and receiver
    if valid:
        first = {"argc": 1, "ret": "0x0" if storage_null_first or not storage else "buf:storage"}
        if handle_after_get is not None:
            first["writes"] = handle_write(handle_after_get)
        c.stub(GET_STORAGE, first)
        if storage_null_first or not storage:
            reloaded = handle if handle_after_get is None else handle_after_get
            if reloaded != INVALID and (reloaded & 0xFFFFFFFF) < capacity:
                resize = {"argc": 2, "argu32": [1]}
                if handle_after_resize is not None:
                    resize["writes"] = handle_write(handle_after_resize)
                c.stub(RESIZE, resize)
                final = reloaded if handle_after_resize is None else handle_after_resize
                if final != INVALID and (final & 0xFFFFFFFF) < capacity:
                    ret = "0x0" if storage_null_after_resize or not storage else "buf:storage"
                    c.stub(GET_STORAGE, {"argc": 1, "ret": ret})

    for i, source in enumerate(sources):
        script_source(c, i, source)

    fixture = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
               "args": {"rdi": "buf:handle", "rsi": "buf:out"}, "buffers": c.buffers, "memory": c.memory,
               "imports": [], "stubs": [entry for queue in c.queues.values() for entry in queue]}
    return fixture


def script_source(c: Case, i: int, s: Source) -> None:
    entry, obj, vtable = f"entry{i}", f"object{i}", f"vtable{i}"
    c.buffer(entry, ENTRY_BYTES, "4b")
    c.put(entry, 0x40, dword(s.state))
    c.buffer(obj, OBJECT_BYTES, "6d")
    c.buffer(vtable, VTABLE_BYTES, "00")
    c.pointer(obj, 0, vtable)
    bare = f"bare_entry{i}"
    if s.no_object_at:
        c.buffer(bare, ENTRY_BYTES, "4b")
        c.put(bare, 0x40, dword(s.state))
        c.put(bare, 0x48, qword(0))
    c.pointer(entry, 0x48, obj)
    getter_names = set()
    for name in METHODS:
        if s.getters and name in VECTOR_GETTER:
            address, offset = VECTOR_GETTER[name]
            getter_names.add(name)
        elif s.getters and name in FLOAT_GETTER:
            address, offset = FLOAT_GETTER[name]
            getter_names.add(name)
        else:
            address = FAKE[name]
        c.memory.append({"addr": f"buf:{vtable}+0x{SLOT[name]:x}", "guest": address})
    if s.getters:
        c.put(obj, 0x10, vec(*s.tint))
        c.put(obj, 0x20, vec(*s.base_force))
        for name, (_, offset) in FLOAT_GETTER.items():
            c.put(obj, offset, f32hex(getattr(s, {"force_scale": "scale", "alternate_extra": "alternate"}.get(name, name))))

    lookup = [0]

    def resolve():
        """Script the next lookup; returns whether it found an entry (and with an object)."""
        lookup[0] += 1
        n = lookup[0]
        if n in s.lookups and s.lookups[n] is None:
            c.stub(RESOLVE, {"argc": 2, "ret": "0x0", "argmem": [{"arg": 1, "size": 8}]})
            return False, False
        target = bare if n in s.no_object_at else entry
        c.stub(RESOLVE, {"argc": 2, "ret": f"buf:{target}", "argmem": [{"arg": 1, "size": 8}]})
        return True, n not in s.no_object_at

    def call(name, ret=0, writes=None, argc=1):
        if name in getter_names:
            return
        script = {"argc": argc, "ret": ret}
        if writes:
            script["writes"] = writes
        c.stub(FAKE[name], script)

    def as_rax(lanes):
        """A scripted stub returns rax, which becomes the result vector's x and y lanes."""
        return f"0x{int.from_bytes(bytes.fromhex(vec(*lanes[:2])), 'little'):x}"

    # Which values the method calls supply (lookups without an entry leave the defaults), to know
    # whether the gust noise is sampled.
    effective = {"base_speed": 0.0, "scale": 1.0}

    found, has_object = resolve()
    if found and s.state == 1:
        return
    found, has_object = resolve()
    if not found or not has_object:
        return
    call("kind", s.kind)
    if (s.kind & 0xFFFFFFFF) == 0xFFFFFFFF:
        return
    found, has_object = resolve()
    if found and has_object:
        call("point_source", s.point)
    if resolve()[0]:
        call("transform", writes=[{"arg": 0, "bytes": "".join(vec(*row) for row in s.orientation)}], argc=2)
    if resolve()[0]:
        call("jitter_angles", argc=3, writes=[{"arg": 1, "bytes": vec(*s.jitter[0])}, {"arg": 2, "bytes": vec(*s.jitter[1])}])
    if resolve()[0]:
        call("base_speed", f"0x{f32(s.base_speed):x}")
        effective["base_speed"] = s.base_speed
    gust = c.carried_gust
    if resolve()[0]:
        gust = c.carried_gust = s.gust
        call("gust_speeds", argc=3, writes=[{"arg": 1, "bytes": f32hex(s.gust[0])}, {"arg": 2, "bytes": f32hex(s.gust[1])}])
    if resolve()[0]:
        call("noise_time", f"0x{f32(s.noise_time):x}")
    if resolve()[0]:
        call("cycle", argc=3, writes=[{"arg": 1, "bytes": f32hex(s.cycle[0])}, {"arg": 2, "bytes": f32hex(s.cycle[1])}])
    if resolve()[0]:
        call("extra", f"0x{f32(s.extra):x}")
    for name, value in (("base_force", as_rax(s.base_force)), ("use_alternate_extra", s.use_alternate),
                        ("alternate_extra", f"0x{f32(s.alternate):x}"), ("force_scale", f"0x{f32(s.scale):x}"),
                        ("tint", as_rax(s.tint))):
        found, has_object = resolve()
        if found and has_object:
            call(name, value)
            if name == "force_scale":
                effective["scale"] = s.scale
    if gust_margin(gust[0], effective["base_speed"], effective["scale"]) > EPSILON:
        c.stub(NOISE, {"argc": 2, "argf32": [0, 1], "argu32": [1], "ret": f"0x{f32(s.noise[0]):x}"})
    c.stub(NOISE, {"argc": 2, "argf32": [0, 1], "argu32": [1], "ret": f"0x{f32(s.noise[1]):x}"})
    if resolve()[0]:
        call("query_0x20", as_rax(s.query))
    if resolve()[0]:
        call("transform", writes=[{"arg": 0, "bytes": "".join(vec(*row) for row in s.transform)}], argc=2)


def live(handle, life=0.5, **kw):
    return dict(life=life, handle=handle, **kw)


def cases():
    out = []
    add = out.append
    point = dict(kind=5)
    # Receiver and storage routes.
    add(build("disabled_system", [Source(0x10)], disabled=1))
    add(build("invalid_handle_empty_list", handle=INVALID))
    add(build("invalid_handle_thirty_hertz", handle=INVALID, mode=0, patch_cell=0.125))
    add(build("handle_past_capacity", handle=7, capacity=4))
    add(build("handle_upper_bits_ignored", [Source(0x11)], handle=0xABCD000000000002, receiver_index=2))
    add(build("no_storage_after_resize", [Source(0x12), Source(0x13, **point)], storage=None))
    add(build("storage_allocated_and_cleared", [Source(0x14)], storage_null_first=True, storage_fill="77"))
    add(build("storage_still_missing_after_resize", [Source(0x15)], storage_null_first=True,
              storage_null_after_resize=True))
    add(build("handle_cleared_by_storage_getter", storage_null_first=True, handle_after_get=INVALID))
    add(build("handle_moved_past_capacity_by_getter", storage_null_first=True, handle_after_get=9))
    add(build("handle_cleared_by_resize", storage_null_first=True, handle_after_resize=INVALID))
    add(build("handle_moved_past_capacity_by_resize", storage_null_first=True, handle_after_resize=6))
    # Settling and the hold flag.
    kept_mix = ([live(0x40, 0.5, seed=1), live(0x41, 0.0, seed=2), live(INVALID, 0.6, seed=8), live(0x43, 2.0, seed=3)],
                [live(0x50, 0.02, seed=4, point=1), live(0x51, -1.0, seed=5), live(INVALID, 0.3, seed=6),
                 live(0x53, QNAN_A, seed=7)])
    add(build("aging_mixed_lives", kept=kept_mix))
    add(build("aging_mixed_lives_sixty_hertz_off", kept=kept_mix, mode=0))
    add(build("aging_with_hold", kept=kept_mix, hold=1))
    add(build("settling_first_call", kept=kept_mix, settling=1))
    add(build("reset_first_call", kept=kept_mix, reset=1, settle_calls=0))
    add(build("settling_later_call", kept=kept_mix, settling=1, reset=1, settle_calls=3))
    add(build("aging_lists_inactive", kept=kept_mix, active=(0, 0)))
    add(build("aging_all_spent", kept=([live(0x40, 0.01)], [live(0x50, 0.0)])))
    add(build("aging_infinite_life", kept=([live(0x40, INF), live(0x41, SNAN)], [live(0x50, 1e30)])))
    add(build("aging_four_live_each", kept=([live(0x40 + i, 0.25 + i, seed=i) for i in range(4)],
                                            [live(0x50 + i, 0.5 + i, seed=i + 4) for i in range(4)])))
    add(build("aging_patch_cell_nan", kept=kept_mix, mode=0, patch_cell=QNAN_B, frame_seconds=QNAN_A))
    add(build("aging_patch_cell_infinite", kept=([live(0x40, INF)], [live(0x50, INF)]), mode=0, patch_cell=INF,
              frame_seconds=INF))
    add(build("aging_negative_frame_time", kept=kept_mix, frame_seconds=-0.5, patch_cell=-0.25, mode=0))

    # Sources: skips.
    add(build("source_ignored_state", [Source(0x20, state=1), Source(0x21)]))
    add(build("source_ignored_state_no_entry", [Source(0x20, state=1, lookups={1: None}), Source(0x21)]))
    add(build("source_no_entry_for_kind", [Source(0x20, lookups={2: None}), Source(0x21)]))
    add(build("source_no_object_for_kind", [Source(0x20, no_object_at={2}), Source(0x21)]))
    add(build("source_no_kind", [Source(0x20, kind=-1), Source(0x21, kind=3)]))
    add(build("source_kind_upper_bits", [Source(0x20, kind=0x7FFFFFFF00000003), Source(0x21, kind=0xFFFFFFFF)]))
    add(build("source_point_flag_upper_bits", [Source(0x20, point=0x100), Source(0x21, point=0x201)]))
    add(build("source_missing_entries_use_defaults",
              [Source(0x22, lookups={3: None, 4: None, 6: None, 8: None, 10: None, 11: None, 12: None, 13: None,
                                     14: None, 15: None, 16: None, 17: None})]))
    add(build("source_alternate_extra", [Source(0x23, use_alternate=1), Source(0x24, use_alternate=0x100)]))
    add(build("source_missing_objects_use_defaults",
              [Source(0x23, no_object_at={3, 11, 12, 13, 14, 15}, use_alternate=1)]))
    # Gusts.
    add(build("gust_base_equals_speed", [Source(0x24, gust=(1.5, 4.0))]))
    add(build("gust_amplitude_equals_speed", [Source(0x24, gust=(3.0, 1.5))]))
    add(build("gust_both_differ_negative_zero", [Source(0x24, gust=(-0.0, 2.0), noise=(0.0, 0.0), base_speed=1.0)]))
    add(build("gust_base_margin_nan", [Source(0x24, gust=(QNAN_A, 2.0))]))
    add(build("gust_amplitude_margin_nan", [Source(0x24, gust=(3.0, QNAN_B))]))
    add(build("gust_scale_zero", [Source(0x24, scale=0.0)]))
    add(build("gust_scale_nan_payloads", [Source(0x24, scale=QNAN_A, base_speed=QNAN_B)]))
    add(build("gust_scale_infinite", [Source(0x24, scale=INF)]))
    add(build("gust_noise_nan", [Source(0x24, noise=(QNAN_A, QNAN_B))]))
    add(build("gust_tiny_margin", [Source(0x24, base_speed=1.0, gust=(1.0000001, 1.0000002), scale=1.0)]))
    # Jitter angles: quadrants, zero, infinity, NaN, huge.
    for name, jitter, noise in (
            ("jitter_zero_angles", ((0.0, -0.0, 0.0, 0.0), (0.0, -0.0, 0.0, 0.0)), (0.5, 0.25)),
            ("jitter_quadrants", ((1.0, 2.5, 4.0, 0.0), (-3.0, -5.0, 7.0, 0.0)), (0.5, 0.15)),
            ("jitter_large_angles", ((100.0, -250.0, 1000.0, 0.0), (300.0, 3000.0, -9000.0, 0.0)), (0.5, 0.11)),
            ("jitter_infinite", ((INF, NEG_INF, 0.0, 0.0), (INF, NEG_INF, 1.0, 0.0)), (0.5, 0.0)),
            ("jitter_nan", ((QNAN_A, 0.5, QNAN_B, 0.0), (1.0, QNAN_A, 2.0, 0.0)), (0.5, 0.3)),
            ("jitter_huge", ((3e9, -3e9, 1e20, 0.0), (3e9, -3e9, 1e20, 0.0)), (0.5, 0.2)),
            ("jitter_negative_zero_noise", ((-1.0, 1.0, -2.0, 0.0), (1.0, 2.0, 3.0, 0.0)), (0.5, -0.0))):
        add(build(name, [Source(0x25, jitter=jitter, noise=noise)]))
    for k, angle in enumerate((0.7853982, 1.5707964, 2.3561945, 3.1415927, 4.712389, 6.2831855, -0.7853982, -2.0)):
        add(build(f"jitter_angle_{k}", [Source(0x26, jitter=((angle, angle * 0.5, -angle, 0.0),) * 2, noise=(0.5, 0.2),
                                               orientation=[(1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0),
                                                            (0.3, -0.4, 0.8, 0.5), (9.0, 9.0, 9.0, 9.0)])]))
    # Cycle wrapping.
    for name, cycle in (("cycle_negative", -2.75), ("cycle_above_one", 3.5), ("cycle_one", 1.0), ("cycle_zero", -0.0),
                        ("cycle_nan", QNAN_A)):
        add(build(name, [Source(0x27, cycle=(5.0, cycle))]))

    # The second list (any kind but 0 and 3, or flagged).
    add(build("other_new_free_slot", [Source(0x30, **point)], kept=([], [live(0x60, 0.4)])))
    add(build("other_new_by_invalid_handle", [Source(0x30, kind=1)],
              kept=([], [live(0x60), live(0x61), live(INVALID, 0.4), live(0x62)])))
    add(build("other_no_free_slot", [Source(0x30, kind=2)], kept=([], [live(0x60 + i, 0.5) for i in range(4)]),
              active=(1, 0)))
    add(build("other_list_full_after_publish", [Source(0x30, kind=2)], kept=([], [live(0x60 + i, 0.5) for i in range(4)])))
    add(build("other_refresh_listed", [Source(0x61, kind=4, point=1)], kept=([], [live(0x60), live(0x61, 0.3)]),
              other_ramps=(0.0, 0.05, 0.0, 0.0)))
    add(build("other_refresh_ramped_full", [Source(0x61, kind=4)], kept=([], [live(0x60), live(0x61, 0.3)]),
              other_ramps=(0.0, 0.5, 0.0, 0.0)))
    add(build("other_refresh_not_listed", [Source(0x61, kind=6)], kept=([], [live(0x60), live(0x61, 0.3)]),
              active=(1, 0), other_ramps=(0.0, 0.02, 0.0, 0.0)))
    add(build("other_refresh_ramp_nan", [Source(0x61, kind=6)], kept=([], [live(0x61, 0.3)]),
              other_ramps=(QNAN_A, 0.0, 0.0, 0.0)))
    add(build("other_list_full", [Source(0x70 + i, kind=7) for i in range(5)]))
    add(build("other_hold_zeroes_later_ramps", [Source(0x61, kind=4), Source(0x62, kind=4)], hold=1,
              kept=([], [live(0x61, 0.3), live(0x62, 0.3)]), other_ramps=(0.03, 0.04, 0.0, 0.0)))
    add(build("other_flagged_kept_still_matches", [Source(0x61, kind=4)], kept=([], [live(0x61, 0.3, flag=1)])))
    add(build("point_flagged_strong_kind", [Source(0x31, kind=3, point=1)]))

    # The first list (kinds 0 and 3).
    add(build("strongest_new", [Source(0x32, kind=0), Source(0x33, kind=3, getters=True)]))
    add(build("strongest_refresh", [Source(0x41, kind=0)], kept=([live(0x40), live(0x41, 0.3)], []),
              ramps=(0.0, 0.07, 0.0, 0.0)))
    add(build("strongest_refresh_not_listed", [Source(0x41, kind=3)], kept=([live(0x41, 0.3)], []), active=(0, 1),
              ramps=(0.01, 0.0, 0.0, 0.0)))
    add(build("strongest_flagged_not_matched", [Source(0x41, kind=0)],
              kept=([live(0x41, 0.3, flag=1), live(0x42, 0.0)], [])))
    add(build("strongest_no_free_slot", [Source(0x45, kind=0)], kept=([live(0x40 + i) for i in range(4)], []),
              active=(0, 1)))
    add(build("strongest_new_by_invalid_handle", [Source(0x45, kind=3)],
              kept=([live(0x40), live(INVALID, 0.4), live(0x42), None], [])))
    add(build("strongest_full_kept_flagged", [Source(0x43, kind=0, scale=60.0)],
              kept=([live(0x40), live(0x41), live(0x42), live(0x43, 0.3, flag=1)], [])))
    add(build("strongest_full_refresh_listed_later", [Source(0x43, kind=3)],
              kept=([live(0x40, 1.0), live(0x41, 0.03), live(0x42, 1.0), live(0x43, 1.0)], []),
              ramps=(0.0, 0.0, 0.0, 0.04)))
    add(build("strongest_no_storage", [Source(0x45, kind=0)], storage=None))
    strong_sources = [Source(0x80 + i, kind=0 if i % 2 else 3, scale=0.5 + i, base_speed=0.25 * i) for i in range(4)]
    add(build("strongest_fill_four", strong_sources))
    add(build("strongest_full_replace_new_slot", strong_sources + [Source(0x90, kind=0, scale=40.0)]))
    add(build("strongest_full_none_weaker", strong_sources + [Source(0x91, kind=0, scale=0.0, base_speed=0.0,
                                                                     base_force=(0.0, 0.0, 0.0, 0.0))]))
    add(build("strongest_full_replace_twice", strong_sources + [Source(0x92, kind=3, scale=40.0),
                                                                Source(0x93, kind=0, scale=80.0)]))
    add(build("strongest_full_replace_kept", [Source(0x40 + i, kind=0) for i in range(4)] + [Source(0x41, kind=0, scale=50.0)],
              kept=([live(0x41, 0.3)], [])))
    add(build("strongest_full_replace_no_room", strong_sources + [Source(0x94, kind=0, scale=60.0)],
              kept=([None] + [live(0x40 + i) for i in range(1, 4)], [])))
    add(build("strongest_listed_at_start_full", [Source(0x95, kind=0, scale=70.0)],
              kept=([live(0x40 + i, 0.3 + i, seed=i) for i in range(4)], [])))
    add(build("strongest_count_past_four_overlaps", strong_sources + [Source(0x96, kind=0, scale=90.0),
                                                                       Source(0x97, kind=3, scale=95.0),
                                                                       Source(0x98, kind=5)],
              kept=([None, None, live(0x40), live(0x41)], [live(0x50), None, None, None])))
    add(build("strongest_nan_force", [Source(0x99, kind=0, base_force=(QNAN_A, QNAN_B, 1.0, 0.0), scale=QNAN_A)]))
    add(build("strongest_full_nan_candidate", strong_sources + [Source(0x9A, kind=0, scale=QNAN_B)]))
    # Values chosen so one operand order, comparison or step source gives different bits.
    add(build("sixty_hertz_mode_patch_cell_set", [Source(0x41, kind=0)], kept=kept_mix, mode=1, patch_cell=0.125))
    add(build("aging_time_nan_payloads", kept=([live(0x40, 0.5, time=QNAN_A)], [live(0x50, 0.5, time=QNAN_B)]),
              mode=0, patch_cell=QNAN_B, frame_seconds=QNAN_A))
    add(build("strongest_free_slot_zero_life", [Source(0x46, kind=0)],
              kept=([live(0x40, 0.0), live(INVALID, 0.5), live(0x42), live(0x43)], []), active=(0, 1)))
    add(build("other_free_slot_zero_life", [Source(0x36, kind=2)],
              kept=([], [live(0x60, 0.0), live(INVALID, 0.5), live(0x62), live(0x63)]), active=(1, 0)))
    add(build("ramp_and_step_nan_payloads", [Source(0x61, kind=6), Source(0x41, kind=0)],
              kept=([live(0x41, 0.3)], [live(0x61, 0.3)]), ramps=(QNAN_B, 0.0, 0.0, 0.0),
              other_ramps=(QNAN_B, 0.0, 0.0, 0.0), mode=0, patch_cell=QNAN_A, frame_seconds=QNAN_A))
    still = dict(scale=0.0, base_speed=0.0, base_force=(0.0, 0.0, 0.0, 0.0))
    add(build("strongest_full_equal_lengths", [Source(0x47, kind=0, **still)],
              kept=([live(0x40 + i, 0.5, force=(0.0, 0.0, 0.0)) for i in range(4)], [])))
    add(build("other_full_kept_source_not_refreshed", [Source(0x61, kind=2)],
              kept=([], [live(0x60 + i, 0.5) for i in range(4)]), other_ramps=(0.01, 0.02, 0.03, 0.04)))
    flat = [(1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0)]
    add(build("gust_negative_zero_survives_add", [Source(0x48, kind=0, gust=(-0.0, -2.0), base_speed=-1.0, scale=1.0,
                                                         noise=(0.0, 0.0), jitter=((0.0,) * 4, (0.0,) * 4),
                                                         base_force=(-0.0, -0.0, -0.0, 0.0), orientation=flat)]))
    add(build("gust_length_infinite", [Source(0x49, kind=0, gust=(1e30, 1e30), noise=(0.5, 0.25))],
              kept=([live(0x49, 0.3)], []), ramps=(0.5, 0.0, 0.0, 0.0)))
    add(build("jitter_one_infinite_angle", [Source(0x4C, kind=0, jitter=((INF, 0.5, -0.25, 0.0),) * 2, noise=(0.5, 0.5))],
              kept=([live(0x4C, 0.3)], []), ramps=(0.5, 0.0, 0.0, 0.0)))
    add(build("direction_nan_payloads", [Source(0x4A, kind=0, orientation=[(1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0),
                                                                          (QNAN_A, 0.0, QNAN_B, 0.0), flat[3]])]))
    add(build("jitter_negative_zero_alone", [Source(0x4B, kind=0, jitter=((-0.0, 0.0, -0.0, 0.0),) * 2, noise=(0.5, 0.5),
                                                    orientation=[flat[0], flat[1], (0.0, -0.0, 0.0, 0.0), flat[3]],
                                                    scale=-1.0, base_force=(-0.0, -0.0, -0.0, 0.0))]))
    # The jitter, gust and cycle values a missing entry leaves from the previous source.
    add(build("missing_entries_keep_previous_out_values",
              [Source(0x20, kind=0, jitter=((0.5, 1.0, -0.75, 0.0), (2.0, 3.0, 1.0, 0.0)), gust=(4.0, 1.25),
                      cycle=(1.0, 0.8125)),
               Source(0x21, kind=1, lookups={5: None, 7: None, 9: None})]))
    add(build("missing_entries_keep_previous_after_skip",
              [Source(0x22, kind=3, gust=(0.5, 3.0), cycle=(2.0, -0.25)), Source(0x23, kind=-1),
               Source(0x24, kind=0, lookups={7: None, 9: None}, gust=(1.5, 1.5))]))
    # Boundaries and NaN-payload orders found by the independent review.
    add(build("gust_amplitude_margin_at_epsilon",
              [Source(0x20, kind=0, base_speed=1.0, scale=1.0, gust=(2.5, 1.0 + 2.0 ** -23))],
              kept=([live(0x20, 0.3)], []), ramps=(0.05, 0.0, 0.0, 0.0)))
    add(build("gust_base_margin_at_epsilon", [Source(0x20, kind=0, base_speed=1.0, scale=1.0, gust=(1.0 + 2.0 ** -23, 2.5))],
              kept=([live(0x20, 0.3)], []), ramps=(0.05, 0.0, 0.0, 0.0)))
    add(build("jitter_nan_lanes", [Source(0x20, kind=0, jitter=((QNAN_A, QNAN_B, 0.25, 0.0), (1.0, 2.0, 3.0, 0.0)))]))
    add(build("jitter_nan_range_and_noise",
              [Source(0x20, kind=0, jitter=((QNAN_A, 0.5, 0.25, 0.0), (1.0, 2.0, 3.0, 0.0)), noise=(0.375, QNAN_B))]))
    add(build("step_and_speed_nan_payloads", [Source(0x20, kind=1, base_speed=QNAN_B)], kept=([], []), mode=0,
              patch_cell=QNAN_A, frame_seconds=QNAN_A))
    add(build("settling_second_call", kept=([live(0x40, 0.5, seed=1), live(0x43, 2.0, seed=3)], [live(0x50, 0.4, seed=4)]),
              settling=1, settle_calls=1))
    # Sources with the invalid handle (the real lookup rejects them) and flagged empty kept slots:
    # the first list's count passes four and a later new source is written past the list.
    add(build("strongest_overflow_invalid_handles",
              [Source(INVALID, kind=0, scale=1.0), Source(0x81, kind=0, scale=2.0), Source(INVALID, kind=3, scale=3.0),
               Source(0x82, kind=0, scale=4.0), Source(INVALID, kind=0, scale=50.0), Source(0x83, kind=3, scale=60.0),
               Source(0x84, kind=2)],
              kept=([live(INVALID, 0.0, flag=1) for _ in range(4)], []), active=(0, 1)))
    # Getter-backed sources with all four lanes.
    add(build("getters_mixed_lists", [Source(0xA0, kind=0, getters=True, tint=(QNAN_A, -0.0, 7.5, INF)),
                                      Source(0xA1, kind=9, getters=True, base_force=(1.0, 2.0, QNAN_B, -4.0))],
              kept=([live(0xA0, 0.4)], [live(0xA1, 0.6)])))
    add(build("getters_hold_and_settle", [Source(0xA2, kind=3, getters=True), Source(0xA3, kind=1, getters=True)],
              hold=1, settling=1, kept=kept_mix))
    add(build("unknown_receiver_sources", [Source(0xA4), Source(0xA5, kind=3)], handle=INVALID, empty_list="sources"))
    return out


def main():
    if len(sys.argv) != 2:
        print(f"usage: {Path(__file__).name} OUT_DIR", file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    fixtures = cases()
    for fixture in fixtures:
        (destination / (fixture["id"] + ".json")).write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(fixtures)} synthetic model wind inputs written to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
