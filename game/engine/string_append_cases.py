#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""NUL-terminated string append; return the original destination."""

import json
from pathlib import Path
import sys

ADDRESS = "0x011c4870"
FORMALS = ["rdi", "rsi"]


def buffers():
    """Distinguishable inactive buffers and sentinels surrounding each supplied byte view."""
    return {
        name: bytearray([value] * 640)
        for name, value in [("destination", 183), ("source", 211), ("arena", 205), ("ignored", 55)]
    }


def span(region, offset, size):
    """A declared input-side byte extent, without expected contents after execution."""
    return {"buffer": region, "offset": offset, "size": size}


def finish(cid, storage, args, reads, writes, incoming=None):
    """Validate all declared views and ignored input slots, then serialize input only."""
    if set(incoming or {}) & set(FORMALS) or set(incoming or {}) - set(args):
        raise ValueError("ignored seeds cannot change formals or unknown slots")
    args.update(incoming or {})
    for view in reads + writes:
        if (
            view["buffer"] not in storage
            or min(view["offset"], view["size"]) < 0
            or view["offset"] + view["size"] > len(storage[view["buffer"]])
        ):
            raise ValueError("declared byte view exceeds buffer")
    for value in args.values():
        if value.startswith("buf:"):
            region, _, at = value[4:].partition("+")
            if region not in storage or not 0 <= int(at or "0", 0) < len(storage[region]):
                raise ValueError("mapped pointer exceeds buffer")
        elif not 0 <= int(value, 0) <= 18446744073709551615:
            raise ValueError("raw GP value exceeds64 bits")
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": "i64",
        "args": args,
        "buffers": {k: {"size": len(v), "bytes": v.hex()} for k, v in storage.items()},
        "memory": [],
        "imports": [],
        "stubs": [],
    }


def seeds():
    """Vary unused incoming registers independently of the formal arguments."""
    return {
        "rdi": "0xfedcba9876543210",
        "rsi": "0x8000000000000001",
        "rdx": "0x76543210fedcba98",
        "rcx": "0xffffffffffffffff",
        "r8": "0x80000000ffffffff",
        "r9": "0x123456789abcdef0",
    }


def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in CASES:
        (out / (row["id"] + ".json")).write_text(json.dumps(row, indent=1) + "\n")
    print(f"{len(CASES)} authored input states written to {out}")
    return 0


def case(
    cid,
    prefix,
    payload,
    destination_offset=17,
    source_offset=8,
    destination_size=None,
    source_size=None,
    alias=None,
    arena_size=640,
    incoming=None,
):
    """Declare finite scans and copy spans from input intent, with no output calculation."""
    if not isinstance(prefix, bytes) or not isinstance(payload, bytes) or b"\x00" in prefix:
        raise ValueError("prefix must exclude zero and payload must be bytes")
    if min(destination_offset, source_offset, arena_size) < 0 or arena_size == 0:
        raise ValueError("negative offset or empty arena")
    zero = payload.find(b"\x00")
    if zero < 0:
        raise ValueError("unbounded append requires finite source terminator")
    scan = len(prefix) + 1
    terminal = destination_offset + len(prefix)
    read_count = write_count = zero + 1
    storage = buffers()
    if destination_size is not None:
        if destination_size < 1:
            raise ValueError("positive destination extent required")
        storage["destination"] = bytearray([183] * destination_size)
    if source_size is not None:
        if source_size < 1:
            raise ValueError("positive source extent required")
        storage["source"] = bytearray([211] * source_size)
    storage["arena"] = bytearray([205] * arena_size)
    if alias is None:
        dr, sr = ("destination", "source")
    elif alias in ("before", "after", "self_zero"):
        dr = sr = "arena"
        if alias == "before" and terminal >= source_offset:
            raise ValueError("before append requires terminal below source")
        if alias == "after" and terminal <= source_offset:
            raise ValueError("after append requires terminal above source")
        if alias == "self_zero" and (terminal != source_offset or zero != 0):
            raise ValueError("self_zero requires source at old destination zero")
        if alias == "after" and zero != 0:
            raise ValueError("unbounded forward after alias may not terminate")
    else:
        raise ValueError("unsupported alias")
    if destination_offset + len(prefix) + 1 > len(storage[dr]) or source_offset + len(
        payload
    ) > len(storage[sr]):
        raise ValueError("initial prefix or payload outside buffer")
    storage[dr][destination_offset : terminal + 1] = prefix + b"\x00"
    if alias is not None:
        overlap_start = max(destination_offset, source_offset)
        overlap_end = min(terminal + 1, source_offset + len(payload))
        if (
            overlap_start < overlap_end
            and storage[dr][overlap_start:overlap_end]
            != payload[overlap_start - source_offset : overlap_end - source_offset]
        ):
            raise ValueError("alias payload conflicts with initial destination scan")
    storage[sr][source_offset : source_offset + len(payload)] = payload
    args = seeds()
    args["rdi"], args["rsi"] = (
        f"buf:{dr}+0x{destination_offset:x}",
        f"buf:{sr}+0x{source_offset:x}",
    )
    reads = [span(dr, destination_offset, scan), span(sr, source_offset, read_count)]
    writes = [span(dr, terminal, write_count)]
    return finish(cid, storage, args, reads, writes, incoming=incoming)


CASES = []
CASES.extend(
    [
        case("empty_both", b"", b"\x00tail"),
        case("empty_source", b"DST", b"\x00tail"),
        case("empty_destination", b"", b"AbC\x00tail"),
        case("ordinary", b"DST:", b"AbC\x00tail"),
        case("high_bytes", b"\x80Q", b"\xff\x81Az\x00tail"),
        case("all_nonzero_bytes", b"X:", bytes(range(1, 256)) + b"\x00"),
        case(
            "exact_span",
            b"D",
            b"ABC\x00",
            destination_offset=0,
            source_offset=0,
            destination_size=5,
            source_size=4,
        ),
        case(
            "self_zero",
            b"ABC",
            b"\x00tail",
            alias="self_zero",
            destination_offset=13,
            source_offset=16,
        ),
        case(
            "alias_before1",
            b"AB",
            b"QRST\x00tail",
            alias="before",
            destination_offset=12,
            source_offset=15,
        ),
        case(
            "alias_before3",
            b"A",
            b"\x80Q\xffRST\x00",
            alias="before",
            destination_offset=8,
            source_offset=12,
        ),
        case(
            "empty_after_alias",
            b"D",
            b"\x00",
            alias="after",
            destination_offset=12,
            source_offset=10,
        ),
        case(
            "ignored_zero",
            b"D:",
            b"Az\x00tail",
            incoming={k: "0x0" for k in seeds() if k not in FORMALS},
        ),
        case(
            "ignored_mapped",
            b"D:",
            b"Az\x00tail",
            incoming={k: "buf:ignored+0x3" for k in seeds() if k not in FORMALS},
        ),
    ]
)
for length in (1, 2, 7, 15, 16, 31, 32, 63, 64, 127):
    CASES.append(
        case(f"bound_{length:03d}", b"P:", bytes((i % 255 + 1 for i in range(length))) + b"\x00")
    )
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(
        case(
            f"unaligned_{offset:02x}",
            b"D:",
            b"\x80QAz\x00tail",
            destination_offset=offset,
            source_offset=offset + 2,
        )
    )
for prefix_length in (7, 16, 63):
    CASES.append(case(f"destination_prefix_{prefix_length:03d}", b"P" * prefix_length, b"xyz\x00"))


if __name__ == "__main__":
    raise SystemExit(main())
