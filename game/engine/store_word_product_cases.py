#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic owner views and scalar inputs; no product or return-value oracle."""

import json
from pathlib import Path
import struct
import sys


def case(
    case_id,
    word,
    factor0,
    factor1,
    owner_offset=16,
    arena_size=96,
    incoming=None,
    pattern=None,
    initial_words=(),
):
    """Supply a complete owner view, including valid unaligned field addresses."""
    if not 0 <= owner_offset <= owner_offset + 0x20 <= arena_size:
        raise ValueError("owner needs its complete writable 0x20-byte extent")
    if any(not 0 <= value <= 0xFFFFFFFFFFFFFFFF for value in (word, factor0, factor1)):
        raise ValueError("incoming GP value outside 64 bits")
    arena = bytearray(
        ((0x6D + 29 * index) & 255) if pattern is None else pattern for index in range(arena_size)
    )
    for offset, value in initial_words:
        if not 0 <= offset <= offset + 4 <= arena_size:
            raise ValueError("initial input word outside supplied arena")
        arena[offset : offset + 4] = struct.pack("<I", value)
    args = {
        "rdi": f"buf:arena+0x{owner_offset:x}",
        "rsi": hex(word),
        "rdx": hex(factor0),
        "rcx": hex(factor1),
        "r8": "0x0",
        "r9": "0x0",
    }
    if set(incoming or {}) & {"rdi", "rsi", "rdx", "rcx"}:
        raise ValueError("ignored seeds cannot replace formal inputs")
    args.update(incoming or {})
    ignored = bytes((0x27 + 7 * index) & 255 for index in range(64))
    return {
        "schema": 1,
        "address": "0x011c4510",
        "id": case_id,
        "returns": "i64",
        "args": args,
        "buffers": {
            "arena": {"size": arena_size, "bytes": arena.hex()},
            "ignored": {"size": 64, "bytes": ignored.hex()},
        },
        "memory": [],
        "imports": [],
        "stubs": [],
    }


INPUT_WORDS = [
    ("ordinary", 0x11223344, 7, 9),
    ("all_zero_words", 0, 0, 0),
    ("first_word_all_bits", 0xFFFFFFFF, 7, 9),
    ("factor0_zero", 0x89ABCDEF, 0, 9),
    ("factor1_zero", 0x89ABCDEF, 7, 0),
    ("unit_factors", 0x01234567, 1, 1),
    ("all_low_words_all_bits", 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF),
    ("factor0_bit31", 0x76543210, 0x80000000, 3),
    ("factor1_bit31", 0x76543210, 3, 0x80000000),
    ("factor0_bit30", 0x76543210, 0x40000000, 1),
    ("factor1_bit30", 0x76543210, 1, 0x40000000),
    ("wrapped_pair", 0x10203040, 0x7FFFFFFF, 0xFFFFFFFE),
    ("mixed_low_words", 0xA1B2C3D4, 0x12345678, 0x9ABCDEF0),
    ("upper_word_first_only", 0xFEDCBA9811223344, 7, 9),
    ("upper_word_factor0_only", 0x11223344, 0x1234567800000007, 9),
    ("upper_word_factor1_only", 0x11223344, 7, 0x8000000000000009),
    ("all_upper_words", 0x0123456789ABCDEF, 0x12345678FEDCBA98, 0x8765432101234567),
    ("all_full_words_all_bits", 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF),
]
CASES = [case(case_id, word, factor0, factor1) for case_id, word, factor0, factor1 in INPUT_WORDS]
for offset in (0, 1, 3, 7, 17, 31):
    CASES.append(case(f"owner_offset_{offset:02x}", 0x11223344, 7, 9, owner_offset=offset))
CASES.extend(
    [
        case(
            "exact_owner_extent",
            0xA1B2C3D4,
            0x12345678,
            0x9ABCDEF0,
            owner_offset=0,
            arena_size=0x20,
        ),
        case("last_owner_extent", 0xA1B2C3D4, 0x12345678, 0x9ABCDEF0, owner_offset=64),
        case(
            "unaligned_last_owner_extent",
            0xA1B2C3D4,
            0x12345678,
            0x9ABCDEF0,
            owner_offset=65,
            arena_size=97,
        ),
        case("zero_canaries", 0x11223344, 7, 9, pattern=0),
        case("all_bits_canaries", 0x11223344, 7, 9, pattern=255),
        case(
            "changed_owner_initial_fields",
            0x11223344,
            7,
            9,
            initial_words=[(40, 0xDEADBEEF), (44, 0x10203040)],
        ),
        case(
            "ignored_gp_all_bits",
            0x11223344,
            7,
            9,
            incoming={"r8": "0xffffffffffffffff", "r9": "0x8000000000000000"},
        ),
        case(
            "ignored_gp_mapped_canaries",
            0x11223344,
            7,
            9,
            incoming={"r8": "buf:ignored+0x1", "r9": "buf:arena+0x3"},
        ),
    ]
)


def main():
    """Write deterministic word-product comparison cases to the requested directory."""
    if len(sys.argv) != 2:
        print(f"usage: {Path(__file__).name} OUT_DIR", file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        path = destination / (fixture["id"] + ".json")
        path.write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(CASES)} authored input states written to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
