#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic GS ring-size writer cases. Usage: SCRIPT OUT_DIR."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from harness import PAGE, SANDBOX  # noqa: E402


ADDRESS = "0x01475af0"
CALLBACK = "0x014712b0"
MASK32, MASK64 = (1 << 32) - 1, (1 << 64) - 1
ITEMSIZE_MASK = 0x7FFF
WORD_BYTES = 4
RESERVATION_WORDS = 17
BUFFER_NAMES = ["descriptor", "packet", "redirect", "self"]
BUFFER_BYTES = 1024
DEFAULT_PREFIX = 128
DEFAULT_CURSOR = "packet+128"
DEFAULT_DESCRIPTOR = "descriptor+128"


def split_pointer(pointer):
    name, _, offset = pointer.partition("+")
    return name, int(offset or "0", 0)


def pointer_value(pointer):
    name, offset = split_pointer(pointer)
    assert name in BUFFER_NAMES and 0 <= offset <= BUFFER_BYTES
    return SANDBOX + BUFFER_NAMES.index(name) * 2 * PAGE + offset


def pointer_words(pointer):
    """A pair of masked payload words can form this owned cursor address."""
    value = pointer_value(pointer)
    words = [value & MASK32, value >> 32]
    assert all(word <= ITEMSIZE_MASK for word in words)
    return words


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def dwords(values):
    return struct.pack("<4I", *(value & MASK32 for value in values)).hex()


def case(cid, values=(1, 2, 4, 8), maximum=3, available=RESERVATION_WORDS,
         result=0, cursor=DEFAULT_CURSOR, descriptor=DEFAULT_DESCRIPTOR,
         prefix=DEFAULT_PREFIX, writes=(), context=0xFEDCBA9876543210,
         byte_span=None):
    """Install an observing callback even for expected no-callback cases."""
    name, offset = split_pointer(cursor)
    span = available * WORD_BYTES if byte_span is None else byte_span
    limit_offset = offset + span
    limit = ({"pointer": f"{name}+{limit_offset}"} if 0 <= limit_offset <= BUFFER_BYTES
             else {"bytes": qword(pointer_value(cursor) + span)})
    argument = {"pointer": context} if isinstance(context, str) else {"bytes": qword(context)}
    return {
        "schema": 1, "address": ADDRESS, "id": cid, "returns": "void",
        "args": {"rdi": f"buf:self+{prefix}", "rsi": "buf:" + descriptor,
                 "rdx": hex(maximum & MASK64)},
        "buffers": {name: {"size": BUFFER_BYTES, "bytes": bytes([0xA5 + i]).hex() * BUFFER_BYTES}
                    for i, name in enumerate(BUFFER_NAMES)},
        "memory": [
            {"addr": f"buf:self+{prefix}", "pointer": "packet"},
            {"addr": f"buf:self+{prefix + 8}", **limit},
            {"addr": f"buf:self+{prefix + 16}", "pointer": cursor},
            {"addr": f"buf:self+{prefix + 24}", "guest": CALLBACK},
            {"addr": f"buf:self+{prefix + 32}", **argument},
            {"addr": "buf:" + descriptor, "bytes": dwords(values)},
        ],
        "stubs": [{"address": CALLBACK, "argc": 3, "argi": [0, 2], "argu32": [1],
                   "ret": hex(result & MASK64), "writes": list(writes)}],
    }


def cases():
    """Vary each contributor, capacity edge and the writer's live reads."""
    boundaries = [0, 1, 2, 3, 0x7F, 0x80, 0xFF, 0x100, 0x3FF, 0x400,
                  0x7FF, 0x800, 0xFFF, 0x1000, 0x3FFF, 0x4000, 0x7FFE,
                  0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x10000, 0x7FFFFFFF, MASK32]
    for stream in range(4):
        for bit in range(32):
            values = [0] * 4
            values[stream] = 1 << bit
            yield case(f"stream-{stream}-bit-{bit:02d}", values, maximum=1)
        for value in boundaries:
            values = [0] * 4
            values[stream] = value
            yield case(f"stream-{stream}-value-{value:08x}", values, maximum=3)
    for bit in range(32):
        yield case(f"maximum-bit-{bit:02d}", (1, 2, 4, 8), 1 << bit)
    for value in boundaries:
        yield case(f"maximum-value-{value:08x}", (1, 2, 4, 8), value)
    for high in [0x100000000, 0x8000000000000000, 0xABCDEF1200000000]:
        for low in [0, 1, 0x800, 0x7FFF]:
            yield case(f"upper-maximum-{high:016x}-{low:04x}", maximum=high | low)
    for values, maximum in [((MASK32,) * 4, MASK32), ((0x7FFF,) * 4, 0x7FF),
                            ((0x7FFF, 1, 0, 0), 3), ((0x4000, 0x4000, 1, 0), 7),
                            ((MASK32, 1, 0, 0), 1), ((0x1234, 0x2345, 0x4567, 0x6789), 0x1234)]:
        yield case("mixed-" + "-".join(f"{word:08x}" for word in (*values, maximum)), values, maximum)
    for words in [-1, 0, 1, 15, 16, 17, 18, 255, 256, 0x40000000, 0x7FFFFFFF,
                  0x80000000, MASK32, 0x100000000, 0x100000010, 0x100000011, (1 << 62) - 1]:
        for result in [0, 0xABCDEF0000000100, 0xABCDEF0000000002]:
            yield case(f"capacity-{words:016x}-{result:016x}", available=words, result=result)
    for span in [-3, -2, -1, 0, 1, 3, 4, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72]:
        yield case(f"byte-span-{span}", byte_span=span)
    for result in [0, 1, 2, 0x80, 0xFF, 0x100, 0x101, 0x8000000000000000]:
        yield case(f"redirect-{result:016x}", available=0, result=result,
                   writes=[
                       {"arg": 0, "offset": 0, "pointer": "redirect", "size": 8},
                       {"arg": 0, "offset": 16, "pointer": "redirect+256", "size": 8},
                       {"arg": 0, "offset": 8, "pointer": "packet", "size": 8},
                       {"arg": 0, "offset": 32, "bytes": qword(0)},
                   ])
    for stream in range(4):
        for result in [0, 2]:
            yield case(f"callback-descriptor-{stream}-{result}", (0, 0, 0, 0),
                       available=0, result=result, context=DEFAULT_DESCRIPTOR,
                       writes=[{"arg": 2, "offset": stream * WORD_BYTES,
                                "bytes": struct.pack("<I", 0x1234 + stream).hex()}])
    for offset in range(80, 213, WORD_BYTES):
        yield case(f"descriptor-alias-{offset:03d}", (0x11, 0x23, 0x45, 0x67), maximum=7,
                   descriptor=f"packet+{offset}")
    # Only the named late descriptor contributor changes; its three neighbors
    # equal the actual packet words written over them, or lie before the packet.
    late = [(128, (1, 0x2D7, 1, 0x2D7)), (124, (0, 1, 0x2D7, 0)),
            (120, (0, 0, 1, 0x2D7)), (116, (0, 0, 0, 1))]
    for stream, (offset, values) in enumerate(late):
        yield case(f"late-word-{stream}", values, maximum=1, descriptor=f"packet+{offset}")
    # The first packet overwrites prefix.cursor with an owned pointer encoded in
    # its first two masked itemsize words. The next packet must follow that read.
    low, high = pointer_words("redirect+128")
    yield case("first-live-cursor", (low, high, 3, 7), maximum=3,
               cursor=f"self+{DEFAULT_PREFIX + 8}")
    # The offset products overwrite prefix.cursor after the first publication.
    low, high = pointer_words("redirect+256")
    yield case("second-live-cursor", (low, (high - low) & ITEMSIZE_MASK, 5, 7), maximum=1,
               cursor=f"self+{DEFAULT_PREFIX - 16}")
    yield case("captured-tail-publication", cursor=f"self+{DEFAULT_PREFIX - 28}")
    yield case("late-total-publication", cursor=f"self+{DEFAULT_PREFIX - 48}")
    yield case("callback-first-live-cursor", (*pointer_words("redirect+128"), 3, 7),
               maximum=3, available=0, result=2,
               writes=[{"arg": 0, "offset": 16,
                        "pointer": f"self+{DEFAULT_PREFIX + 8}", "size": 8}])
    yield case("callback-second-live-cursor", (low, (high - low) & ITEMSIZE_MASK, 5, 7),
               maximum=1, available=0, result=2,
               writes=[{"arg": 0, "offset": 16,
                        "pointer": f"self+{DEFAULT_PREFIX - 16}", "size": 8}])
    for offset in [1, 2, 3, 5, 7]:
        yield case(f"unaligned-packet-{offset}", cursor=f"packet+{128 + offset}")
        yield case(f"unaligned-descriptor-{offset}", descriptor=f"descriptor+{128 + offset}")
        yield case(f"unaligned-prefix-{offset}", prefix=DEFAULT_PREFIX + offset, available=0, result=2)
    for pointer in ["0x0", "0xdeadbeefdeadbeef"]:
        for result in [0, 0xABCDEF0000000100]:
            entry = case(f"decline-unused-descriptor-{pointer}-{result:016x}", available=0, result=result)
            entry["args"]["rsi"] = pointer
            yield entry


def main():
    """Write deterministic, unique synthetic dictionaries outside this tree."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len({entry["id"] for entry in generated}) == len(generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
