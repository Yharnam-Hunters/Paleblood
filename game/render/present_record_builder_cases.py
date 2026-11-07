#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic normalized builder paths and ordered auxiliary publications.

Usage: present_record_builder_cases.py OUT_DIR. Controlled lower calls expose
counts and constructor selectors; these authored edge inputs are not recordings.
"""
import json
from itertools import chain
from pathlib import Path
import struct
import sys

ADDRESS = "0x0256e1d0"
RETURN = "i32"
INPUTS = [
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65792), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936704), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65792), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936704), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65792), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936704), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65792), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936704), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65792), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936704), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65792), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936704), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65794), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936706), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65794), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936706), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65794), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936706), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65794), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936706), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65794), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936706), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65794), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936706), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65795), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936707), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65795), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936707), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65795), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936707), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65795), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936707), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65795), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936707), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65795), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936707), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65796), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936708), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65796), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936708), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65796), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936708), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65796), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936708), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65796), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936708), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 65796), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936708), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 66047), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 0, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936959), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 66047), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 19, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936959), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 66047), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 20, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936959), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 66047), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 22, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936959), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 66047), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 40, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936959), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 66047), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
    {
        "args": ["p32", "p128", 64, 0, "p384", 1],
        "words": [(136, 0), (140, 0), (160, 536936959), (164, 0), (168, 2), (172, 2097216), (176, 65537), (180, 65537)],
        "report_stub": False,
    },
]


def packed_bytes(size, words=(), qwords=()):
    b = bytearray(size)
    for off, v in words:
        struct.pack_into("<I", b, off, v)
    for off, v in qwords:
        struct.pack_into("<Q", b, off, v)
    return b.hex()


def call(address, argc, ret=0, writes=(), u32=(), u8=()):
    r = {"address": address, "argc": argc, "ret": ret}
    if writes:
        r["writes"] = list(writes)
    if u32:
        r["argu32"] = list(u32)
    if u8:
        r["argu8"] = list(u8)
    # Compare pointers at full width; selectors occupy DWORD stack slots.
    if address in {"0x01487ee0", "0x01488590"}:
        r["argi"] = [0]
    elif address == "0x01485f90":
        r["argi"] = [0, 8, 9]
    elif address == "0x0149e2d0":
        r["argi"] = [0, 1, 2, 3]
    return r


def normalized_payload(mode, first, second, third, count, flags, first_kind=1, second_kind=0, normalized_mode_word=0):
    if mode == 2:
        values = [
            (0, first),
            (4, count),
            (8, third),
            (12, 0x16),
            (16, 1),
            (20, 0x20),
            (24, 0),
            (28, flags),
            (36, 0xFAC001),
            (40, 4),
        ]
        size = 0x3C
    elif mode == 3:
        values = [
            (0, first),
            (4, second),
            (8, count),
            (12, third),
            (16, 0x16),
            (20, first_kind),
            (24, second_kind),
            (28, 1),
            (32, 0x20),
            (36, 0),
            (40, flags),
            (48, 0xFAC001),
            (52, 4),
            # The low byte is the mode; high-only words exercise adjacent padding.
            (0x40, normalized_mode_word),
        ]
        size = 0x48
    else:
        values = [
            (0, first),
            (4, second),
            (8, third),
            (12, count),
            (16, 0x16),
            (20, 1),
            (24, 0x20),
            (28, 0),
            (32, flags),
            (40, 0xFAC001),
            (44, 4),
        ]
        size = 0x40
    return packed_bytes(size, values)


def geometry_result():
    return packed_bytes(0x38, [(0, 64), (4, 64), (8, 3), (0x1C, 0x100), (0x2C, 64), (0x30, 4)], [(0x10, 0x10000)])


def level_descriptor():
    return packed_bytes(0x30, [(0, 4), (8, 64), (12, 64), (16, 3), (20, 1), (40, 64)])


def controlled_input(label, mode, first=64, second=32, third=3, count=1, flags=0, index=0, short=False, normalized_mode_word=0):
    norm_address = {2: "0x025710a0", 3: "0x025711d0", 4: "0x02571340"}[mode]
    scripts = [
        call(
            norm_address,
            4,
            writes=[
                {"arg": 0, "bytes": normalized_payload(mode, first, second, third, count, flags, 2 if short else 1,
                                            normalized_mode_word=normalized_mode_word)}
            ],
            u32=[3],
        )
    ]
    # Selector calls remain observable; scripted scalar results keep division defined.
    for _ in range(8):
        scripts.append(call("0x02ab7a20", 2, 1, u32=[0, 1]))
    for _ in range(12):
        scripts.append(call("0x02ab7ab0", 3, 1, u32=[0, 1, 2]))
    scripts.append(call("0x02ab7e40", 1, 1, u32=[0]))
    if short:
        scripts.append(
            call(
                "0x01485f90",
                10,
                "0x0000010000000123",
                writes=[
                    {"arg": 0, "bytes": packed_bytes(0x2C, [(0, 0x12345678)])},
                    {"arg": 8, "bytes": struct.pack("<Q", 0x10000000080).hex()},
                    {"arg": 9, "bytes": struct.pack("<Q", 0x20000000200).hex()},
                ],
                u32=range(1, 8),
            )
        )
    else:
        for _ in range(4):
            if mode == 4:
                scripts.append(
                    call(
                        "0x01488590",
                        7,
                        "0x0000010000000123",
                        writes=[{"arg": 0, "bytes": packed_bytes(0x20)}],
                        u32=range(1, 7),
                    )
                )
            else:
                scripts.append(
                    call(
                        "0x01487ee0",
                        9,
                        "0x0000010000000123",
                        writes=[{"arg": 0, "bytes": packed_bytes(0x20)}],
                        u32=range(1, 8),
                        u8=[8],
                    )
                )
        for _ in range(index + 2):
            scripts.append(call("0x014a1e80", 4, writes=[{"arg": 0, "bytes": level_descriptor()}], u32=[2, 3]))
            scripts.append(call("0x0149ce10", 2, writes=[{"arg": 0, "bytes": geometry_result()}]))
        constructor_writes = [{"arg": 0, "bytes": packed_bytes(0x2C, [(0, 0x12345678)])}]
        if flags & 0x600:
            constructor_writes.append({"arg": 8, "bytes": struct.pack("<Q", 0x10000000080).hex()})
        scripts.append(call("0x01485f90", 10, writes=constructor_writes, u32=range(1, 8)))
        if index:
            scripts.append(
                call(
                    "0x014a2c40",
                    5,
                    writes=[
                        {"arg": 0, "bytes": struct.pack("<Q", 0x100).hex()},
                        {"arg": 1, "bytes": struct.pack("<Q", 0x1000).hex()},
                    ],
                    u32=[3, 4],
                )
            )
        if flags & 0x600:
            for previous in range(index + 1):
                unit = 0x400 if previous < index else 0x100
                scripts.append(
                    call(
                        "0x0149e2d0",
                        8,
                        writes=[
                            {"arg": 0, "bytes": struct.pack("<Q", 0x123).hex()},
                            {"arg": 1, "bytes": struct.pack("<I", unit).hex()},
                            {"arg": 2, "bytes": struct.pack("<I", 64).hex()},
                            {"arg": 3, "bytes": struct.pack("<I", 64).hex()},
                        ],
                        u32=[4, 5, 6],
                        u8=[7],
                    )
                )
    return {
        "id": label,
        "args": ["p32", "p128", 0x16, index, 7, 3],
        "words": [
            (136, 0x10000),
            (140, 0),
            (160, mode),
            (164, flags),
            (168, 4),
            (172, 0x00200040),
            (176, 0x00010001),
            (180, 0x00010001),
        ],
        "stubs": scripts,
    }


def controlled_inputs():
    yield controlled_input("mode_three_short_construction", 3, flags=0x400, short=True)
    for mode in (2, 3, 4):
        for flag in (0x400, 0x200):
            for index in (1, 2):
                yield controlled_input(f"mode_{mode}_auxiliary_{flag:03x}_index_{index}", mode, flags=flag, index=index)
        if mode == 2:
            yield controlled_input("mode_two_zero_derived_count", mode, first=0, count=0)
            yield controlled_input("mode_two_nonzero_derived_count", mode, first=128, count=0)
        elif mode == 3:
            yield controlled_input("mode_three_zero_derived_count", mode, first=0, second=0, count=0)
            yield controlled_input("mode_three_nonzero_derived_count", mode, first=128, second=32, count=0)
        else:
            yield controlled_input("mode_four_zero_derived_count", mode, first=0, second=0, third=0, count=0)
            yield controlled_input("mode_four_third_sets_derived_count", mode, first=0, second=0, third=128, count=0)
        yield controlled_input(f"mode_{mode}_explicit_count_override", mode, first=0, second=0, third=0, count=7)


def transformed_inputs():
    """Vary both contributors to the mode-3 transform and observe the pair call."""
    # Each factor also has a case with the other factor present and this one clear.
    # Noncanonical mode bytes must remain nonzero; adjacent padding must not count.
    inputs = [
        ("mode_three_transform_mode_zero_flags_zero", 0, 0, 3, 0),
        ("mode_three_transform_mode_zero_flags_four", 0, 4, 3, 0),
        ("mode_three_transform_mode_one_flags_zero", 1, 0, 3, 0),
        ("mode_three_transform_mode_one_flags_four_third_three", 1, 4, 3, 0),
        ("mode_three_transform_mode_two_flags_four", 2, 4, 7, 0),
        ("mode_three_transform_mode_high_byte_flags_four", 0x80, 4, 7, 0),
        ("mode_three_transform_mode_ff_flags_four_third_zero", 0xff, 4, 0, 0),
        ("mode_three_transform_padding_bit_eight", 0x100, 4, 3, 0),
        ("mode_three_transform_padding_bit_sixteen", 0x10000, 4, 3, 0),
        ("mode_three_transform_padding_bit_thirty_one", 0x80000000, 4, 3, 0),
        ("mode_three_transform_neighbor_flag_two", 1, 2, 3, 0),
        ("mode_three_transform_neighbor_flag_eight", 1, 8, 3, 0),
        ("mode_three_transform_high_only_flag", 1, 0x10000, 3, 0),
        ("mode_three_transform_flag_four_plus_two", 1, 6, 3, 0),
        ("mode_three_transform_flag_four_plus_eight", 1, 12, 3, 0),
        ("mode_three_transform_flag_four_plus_high", 1, 0x10004, 3, 0),
        ("mode_three_transform_third_five", 1, 4, 5, 0),
        ("mode_three_transform_third_six", 1, 4, 6, 0),
        ("mode_three_transform_third_max_input_half", 1, 4, 0xffff, 0),
        ("mode_three_transform_offset_index_one", 1, 4, 1, 1),
        ("mode_three_transform_first_auxiliary", 1, 0x404, 7, 1),
        ("mode_three_transform_second_auxiliary", 1, 0x204, 7, 1),
        ("mode_three_transform_padding_plus_high_flag", 0x100, 0x10004, 7, 0),
    ]
    for label, mode_word, flags, third, index in inputs:
        yield controlled_input(label, 3, third=third, flags=flags, index=index,
                               normalized_mode_word=mode_word)


def cases():
    """Yield builder inputs and observe lower-call arguments at their logical widths."""
    registers = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"]
    for index, inputs in enumerate(chain(INPUTS, controlled_inputs(), transformed_inputs())):
        arena = bytearray((0x35 + 29 * i) & 255 for i in range(512))
        for offset, value in inputs.get("words", []):
            struct.pack_into("<I", arena, offset, value & 0xFFFFFFFF)

        def wire(value):
            return (
                "buf:arena+0x" + format(int(value[1:]), "x")
                if isinstance(value, str) and value.startswith("p")
                else hex(value)
            )

        values = [wire(v) for v in inputs["args"]]
        args = {register: (values[i] if i < len(values) else "0x0") for i, register in enumerate(registers)}
        args.update({k: wire(v) for k, v in inputs.get("incoming", {}).items()})
        case = {
            "schema": 1,
            "address": ADDRESS,
            "id": inputs.get("id", "edge_" + format(index, "04d")),
            "returns": inputs.get("returns", RETURN),
            "args": args,
            "stack": values[6:],
            "buffers": {"arena": {"size": len(arena), "bytes": arena.hex()}},
            "memory": [],
            "imports": [],
            "stubs": inputs.get(
                "stubs", ([{"address": "0x02abb4a0", "argc": 1, "ret": "0x0"}] if inputs.get("report_stub") else [])
            ),
        }
        yield case


def main():
    """Write the deterministic synthetic cases to the requested directory."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for case in generated:
        (destination / (case["id"] + ".json")).write_text(json.dumps(case, indent=1) + "\n")
    print(str(len(generated)) + " synthetic cases written")


if __name__ == "__main__":
    sys.exit(main())
