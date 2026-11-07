# SPDX-License-Identifier: GPL-2.0-or-later
"""Build synthetic inputs for FMOD output descriptor callbacks."""

import struct


def make_case(
    cid, offsets, arena_size=64, pattern=None, incoming=None, initial_words=(), *, address, outputs
):
    """Supply buffer views and incoming slots without deriving source outcomes."""
    if len(offsets) != len(outputs):
        raise ValueError("one supplied view per output slot required")
    arena = bytearray(
        (53 + 17 * index & 255 if pattern is None else pattern for index in range(arena_size))
    )
    for offset, value in initial_words:
        if not 0 <= offset <= offset + 4 <= arena_size:
            raise ValueError("initial DWORD outside supplied buffer")
        arena[offset : offset + 4] = struct.pack("<I", value)
    args = {register: "0x0" for register in ("rdi", "rsi", "rdx", "rcx", "r8", "r9")}
    for register, offset in zip(outputs, offsets):
        if offset is not None and (not 0 <= offset <= offset + 4 <= arena_size):
            raise ValueError("output view needs exactly four mapped writable bytes")
        args[register] = "0x0" if offset is None else f"buf:arena+0x{offset:x}"
    if set(incoming or {}) & set(outputs):
        raise ValueError("ignored incoming seeds cannot replace output views")
    args.update(incoming or {})
    fixture = {
        "schema": 1,
        "address": address,
        "id": cid,
        "returns": "i64",
        "args": args,
        "buffers": {
            "arena": {"size": arena_size, "bytes": arena.hex()},
            "ignored": {"size": 64, "bytes": bytes((113 + 3 * i & 255 for i in range(64))).hex()},
        },
        "memory": [],
        "imports": [],
        "stubs": [],
    }
    return fixture
