#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded string copy used by FMOD callers; stop at NUL or count, without padding."""

import json
from pathlib import Path
import sys

ADDRESS = "0x011c4840"


def case(
    cid,
    payload,
    count_input=None,
    destination_offset=16,
    source_offset=8,
    destination_size=None,
    source_size=None,
    alias=None,
    arena_size=128,
    incoming=None,
    opaque=None,
):
    """Build input bytes only; derive span bounds from input termination/count or explicit bounded alias."""
    if not isinstance(payload, bytes):
        raise ValueError("payload must be bytes")
    if not isinstance(count_input, int) or not 0 <= count_input <= 18446744073709551615:
        raise ValueError("count must be a full64 input word")
    count = count_input & 4294967295
    first_zero = payload.find(b"\x00")
    access = first_zero + 1 if first_zero >= 0 else len(payload)
    access = min(count, access)
    if first_zero < 0 and len(payload) < count:
        raise ValueError("unterminated source shorter than count")
    if alias == "after":
        access = count
    if opaque is not None and (count != 0 or alias is not None):
        raise ValueError("opaque formal pointers require counted zero access")
    if min(destination_offset, source_offset) < 0:
        raise ValueError("negative byte offset")
    destination_size = (
        max(64, destination_offset + access + 8) if destination_size is None else destination_size
    )
    source_size = max(64, source_offset + len(payload) + 8) if source_size is None else source_size
    if min(destination_size, source_size, arena_size) < 1:
        raise ValueError("buffers must have positive extent")
    buffers = {
        "destination": bytearray([165] * destination_size),
        "source": bytearray([211] * source_size),
        "arena": bytearray([183] * arena_size),
        "ignored": bytearray((49 + 17 * index & 255 for index in range(64))),
    }
    if alias is None:
        source_region, destination_region = ("source", "destination")
    elif alias in ("self", "before", "after"):
        source_region = destination_region = "arena"
        if alias == "self" and destination_offset != source_offset:
            raise ValueError("self alias needs equal offsets")
        if alias == "before" and destination_offset >= source_offset:
            raise ValueError("before alias needs smaller destination")
        if alias == "after" and destination_offset <= source_offset:
            raise ValueError("after alias needs larger destination")
    else:
        raise ValueError("unknown alias domain")
    if source_offset + len(payload) > len(buffers[source_region]):
        raise ValueError("initial payload outside source region")
    if source_offset + access > len(buffers[source_region]) or destination_offset + access > len(
        buffers[destination_region]
    ):
        raise ValueError("declared reached access span outside buffer")
    buffers[source_region][source_offset : source_offset + len(payload)] = payload
    args = {
        "rdi": f"buf:{destination_region}+0x{destination_offset:x}",
        "rsi": f"buf:{source_region}+0x{source_offset:x}",
        "rdx": hex(count_input),
        "rcx": "0xfedcba9876543210",
        "r8": "0x8000000000000001",
        "r9": "0xffffffffffffffff",
    }
    if opaque is not None:
        if len(opaque) != 2 or any((not isinstance(x, str) for x in opaque)):
            raise ValueError("opaque pair must contain pointer strings")
        args["rdi"], args["rsi"] = opaque
    formal = {"rdi", "rsi", "rdx"}
    if set(incoming or {}) & formal:
        raise ValueError("ignored seeds cannot override formal inputs")
    if set(incoming or {}) - (set(args) - formal):
        raise ValueError("unsupported ignored GP seed")
    args.update(incoming or {})
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": "i64",
        "args": args,
        "buffers": {
            name: {"size": len(data), "bytes": data.hex()} for name, data in buffers.items()
        },
        "memory": [],
        "imports": [],
        "stubs": [],
    }


CASES = [
    case("zero_null", b"", 0, opaque=("0x0", "0x0")),
    case("zero_opaque", b"", 0, opaque=("0xfedcba9876543210", "0x8000000000000001")),
    case(
        "upper32_zero_opaque",
        b"",
        16045690981097406464,
        opaque=("0x8765432100000001", "0x8000000000000000"),
    ),
    case("zero_mapped", b"ABCD\x00", 0),
    case("zero_null_source", b"", 0, opaque=("buf:destination+0x11", "0x0")),
    case("zero_null_destination", b"", 0, opaque=("0x0", "buf:source+0x3")),
    case("count_one_nonzero", b"ABCD\x00", 1),
    case("count_one_zero", b"\x00unused", 1),
    case("count_before_zero", b"AB\x00unused", 2),
    case("count_includes_zero", b"AB\x00unused", 3),
    case("count_after_zero", b"AB\x00unused", 7),
    case("upper32_two", b"ABCD\x00", 1311768464867721218),
    case("upper32_one", b"ABCD\x00", 18446744069414584321),
    case("max32_early_zero", b"Q\x00", 4294967295),
    case("max64_early_zero", b"\x80Q\xff\x00", 18446744073709551615),
    case("bit31_early_zero", b"Q\x00", 2147483648),
    case("high_bytes", b"\x80\xff\xfe\x81\x00", 5),
    case("high_bytes_no_terminator", b"\x80\xff\xfe\x81", 4),
]
for length in (1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256):
    CASES.append(
        case(
            f"bound_{length:03d}",
            bytes((index % 255 + 1 for index in range(length))) + b"\x00tail",
            length,
        )
    )
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(
        case(
            f"unaligned_{offset:02x}",
            b"\x80Q\xffR\x00",
            5,
            destination_offset=offset,
            source_offset=offset + 2,
        )
    )
CASES.extend(
    [
        case(
            "exact_one_byte_buffers",
            b"Q",
            1,
            destination_offset=0,
            source_offset=0,
            destination_size=1,
            source_size=1,
        ),
        case(
            "exact_bound_buffers",
            b"ABCD",
            4,
            destination_offset=0,
            source_offset=0,
            destination_size=4,
            source_size=4,
        ),
        case(
            "exact_terminator_buffers",
            b"ABCD\x00",
            5,
            destination_offset=0,
            source_offset=0,
            destination_size=5,
            source_size=5,
        ),
        case(
            "last_terminator",
            b"\x80AB\xff\x00",
            5,
            destination_offset=17,
            source_offset=9,
            destination_size=22,
            source_size=14,
        ),
        case("self_alias", b"ABCD\x00", 5, alias="self", destination_offset=17, source_offset=17),
        case(
            "self_alias_count_only",
            b"\xff\x80\xfe\x81",
            4,
            alias="self",
            destination_offset=3,
            source_offset=3,
        ),
        case(
            "alias_before_1",
            b"ABCD\x00",
            5,
            alias="before",
            destination_offset=16,
            source_offset=17,
        ),
        case(
            "alias_before_3",
            b"\x80AB\xffC\x00",
            6,
            alias="before",
            destination_offset=8,
            source_offset=11,
        ),
        case(
            "alias_after_chain1",
            b"K\x00QRSTUV",
            6,
            alias="after",
            destination_offset=9,
            source_offset=8,
        ),
        case(
            "alias_after_chain2",
            b"\x80Q\x00RSTUV",
            6,
            alias="after",
            destination_offset=10,
            source_offset=8,
        ),
        case(
            "alias_after_chain3",
            b"\x80Q\xff\x00STUV",
            7,
            alias="after",
            destination_offset=11,
            source_offset=8,
        ),
        case(
            "empty_after_alias",
            b"\x00ABCD",
            4,
            alias="after",
            destination_offset=10,
            source_offset=9,
        ),
        case("ignored_gp_zero", b"ABCD\x00", 5, incoming={"rcx": "0x0", "r8": "0x0", "r9": "0x0"}),
        case(
            "ignored_gp_mapped",
            b"ABCD\x00",
            5,
            incoming={"rcx": "buf:arena+0x3", "r8": "buf:source+0x2", "r9": "buf:ignored+0x7"},
        ),
    ]
)


def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        print(f"usage: {Path(__file__).name} OUT_DIR", file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (output / (fixture["id"] + ".json")).write_text(json.dumps(fixture, indent=1) + "\n")
    print(f"{len(CASES)} authored input states written to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
