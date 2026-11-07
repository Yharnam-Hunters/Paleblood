#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: present_shader_secondary_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys

MASK64 = (1 << 64) - 1


def word(value):
    return struct.pack("<I", value & 0xFFFFFFFF).hex()


def qword(value):
    return struct.pack("<Q", value & MASK64).hex()


def memory(offset, value):
    return {"addr": f"buf:self+{offset}", "bytes": word(value)}


def basic(cid, returns="i64", size=512):
    return {
        "schema": 1,
        "address": ADDRESS,
        "id": cid,
        "returns": returns,
        "args": {"rdi": "buf:self"},
        "buffers": {"self": {"size": size, "bytes": "a5" * size}},
        "memory": [],
        "stubs": [],
    }


ADDRESS = "0x02ace5e0"


def cases():
    for mode in (0, 1):
        for typ in (6, 12, 19, 25, 99):
            for optional in (False, True):
                c = basic(f"mode_{mode}_type_{typ}_optional_{int(optional)}", returns="void", size=0xB400)
                c["buffers"]["self"]["bytes"] = "00" * 0xB400
                c["buffers"].update(
                    {
                        "context": {"size": 0xB500},
                        "descriptor": {"size": 256},
                        "entries": {"size": 16, "bytes": word(typ | (16 << 8))},
                        "guard": {"size": 16},
                    }
                )
                c["args"].update(
                    {"rsi": "buf:context", "rdx": "buf:descriptor", "rcx": "buf:descriptor" if optional else 0}
                )
                c["memory"] = [
                    {"addr": "0x057e3be0", "pointer": "guard"},
                    memory(0, mode),
                    {"addr": "buf:self+48", "bytes": qword(1)},
                    {"addr": "buf:self+6448", "bytes": qword(1)},
                    {"addr": "buf:descriptor+224", "pointer": "entries"},
                    {"addr": "buf:descriptor+232", "bytes": word(1)},
                ]
                if mode:
                    c["stubs"].append({"address": "0x01476030", "argc": 2, "ret": 0})
                c["stubs"].append({"address": "0x02ab9850", "argc": 2, "ret": 0})
                if typ == 6:
                    c["stubs"].append({"address": "0x01476420", "argc": 4, "ret": 0})
                    if optional:
                        c["stubs"].append({"address": "0x01476420", "argc": 4, "ret": 0})
                yield c

    def binding_case(cid, kind, bank=0, allocation=True, cache_hit=False, count=1, multiple=False):
        c = basic(cid, returns="void", size=0xB400)
        c["buffers"]["self"]["bytes"] = "00" * 0xB400
        work = (1 | (1 << 63)) if multiple else 1
        entries = b"".join(struct.pack("<I", kind | (16 << 8) | (2 << 16) | (3 << 24)) for _ in range(64))
        c["buffers"].update(
            {
                "context": {"size": 0xB500},
                "descriptor": {"size": 256},
                "entries": {"size": 256, "bytes": entries.hex()},
                "upload": {"size": 8192, "bytes": "73" * 8192},
                "alternate": {"size": 8192, "bytes": "94" * 8192},
            }
        )
        c["args"].update({"rsi": "buf:context", "rdx": "buf:descriptor", "rcx": "buf:descriptor" if bank else 0})
        base = 0x1900 if bank else 0
        c["memory"] = [
            memory(0, 0),
            {"addr": f"buf:self+{base+0x30}", "bytes": qword(work)},
            {"addr": f"buf:self+{base+0x60}", "bytes": qword(0x28) + qword(0)},
            {
                "addr": f"buf:self+{base+0x70}",
                "bytes": qword((1 << 38) | (1 << 40) | (1 << 1) | (1 << 3) | (1 << 18) | (1 << 20)),
            },
            {"addr": f"buf:self+{base+0x18}", "bytes": qword(0xFFFFFFFFFFFFFFFF) * 3},
            {"addr": "buf:descriptor+224", "pointer": "entries"},
            {"addr": "buf:descriptor+232", "bytes": word(count)},
        ]
        # Distinguish neighbouring 16-byte and 32-byte records and every copied word.
        origins = (0x68E0, 0x90E0, 0x98E0, 0x9FE0, 0xA6A0) if bank else (0x28E0, 0x88E0, 0x94E0, 0x9AE0, 0xA8A0)
        for family, origin in enumerate(origins):
            c["memory"].append(
                {
                    "addr": f"buf:self+{origin}",
                    "bytes": b"".join(
                        struct.pack("<I", 0x10203040 + family * 0x10000 + i * 0x101) for i in range(64)
                    ).hex(),
                }
            )
        c["stubs"] = [{"address": "0x02ab9850", "argc": 2, "ret": 0}]
        descriptor_calls = {
            0: "0x014761a0",
            1: "0x01476240",
            2: "0x014761a0",
            3: "0x01476240",
            4: "0x014762f0",
            5: "0x014761a0",
            6: "0x01476420",
        }
        repeats = 2 if multiple else 1
        for repeat in range(repeats):
            if kind in descriptor_calls:
                stub = {"address": descriptor_calls[kind], "argc": 4, "ret": 0, "argu32": [1, 2]}
                if kind == 6:
                    stub["argu32"].append(3)
                c["stubs"].append(stub)
            elif kind in (7, 20):
                if cache_hit:
                    c["memory"].append({"addr": "buf:self+45920", "pointer": "upload"})
                elif repeat == 0:
                    c["stubs"].append({"address": "0x02aaddc0", "argc": 3, "ret": "buf:upload" if allocation else 0})
                    if allocation:
                        # The copy can change the shared cache through its source alias.
                        c["stubs"].append(
                            {
                                "address": "0x02fbe578",
                                "argc": 3,
                                "ret": "buf:upload",
                                "writes": [{"arg": 1, "offset": 192, "pointer": "alternate", "size": 8}],
                            }
                        )
                elif not allocation:
                    c["stubs"].append({"address": "0x02aaddc0", "argc": 3, "ret": 0})
                if kind == 7:
                    c["stubs"].append({"address": "0x01476390", "argc": 4, "argu32": [1, 2], "ret": 0})
            elif kind in (8, 9, 10, 11, 21, 22, 23, 24, 26):
                c["stubs"].append({"address": "0x02aaddc0", "argc": 3, "ret": "buf:upload" if allocation else 0})
                if allocation:
                    c["stubs"].append({"address": "0x02fbe578", "argc": 3, "ret": "buf:upload"})
                if kind in (8, 9, 10, 11, 26):
                    c["stubs"].append({"address": "0x01476390", "argc": 4, "argu32": [1, 2], "ret": 0})
        return c

    for bank in (0, 1):
        for kind in range(27):
            yield binding_case(f"all_kinds_bank_{bank}_kind_{kind}", kind, bank)
        for kind in (7, 8, 9, 10, 11, 20, 21, 22, 23, 24, 26):
            yield binding_case(f"failed_upload_bank_{bank}_kind_{kind}", kind, bank, allocation=False)
        for kind in (7, 20):
            yield binding_case(f"cache_hit_bank_{bank}_kind_{kind}", kind, bank, cache_hit=True)
        for kind in (0, 4, 6, 7, 13, 17, 20, 26):
            yield binding_case(f"multiple_bits_count64_bank_{bank}_kind_{kind}", kind, bank, count=64, multiple=True)

    for bank in (0, 1):
        for kind in (8, 9, 10, 11, 21, 22, 23, 24):
            c = binding_case(f"empty_span_bank_{bank}_kind_{kind}", kind, bank)
            base = 0x1900 if bank else 0
            c["memory"].append({"addr": f"buf:self+{base+0x60}", "bytes": "00" * 24})
            c["stubs"] = [{"address": "0x02ab9850", "argc": 2, "ret": 0}]
            yield c
        yield binding_case(f"multiple_bits_count65_bank_{bank}", 6, bank, count=65, multiple=True)
    c = binding_case("cache_shared_across_both_banks", 7)
    c["args"]["rcx"] = "buf:descriptor"
    c["memory"].append({"addr": "buf:self+6448", "bytes": qword(1)})
    c["stubs"].append({"address": "0x01476390", "argc": 4, "argu32": [1, 2], "ret": 0})
    yield c

    for bank in (0, 1):
        base = 0x1900 if bank else 0
        for kind in (8, 21):
            for low, high in ((0, 3), (1, 3)):
                c = binding_case(f"wide_span_bank_{bank}_kind_{kind}_low_{low}", kind, bank)
                c["memory"].append({"addr": f"buf:self+{base+0x60}", "bytes": qword(low) + qword(high)})
                # The source span and prefix offset can reach beyond the short seeded examples.
                for name in ("upload", "alternate"):
                    c["buffers"][name]["size"] = 16384
                    c["buffers"][name]["bytes"] = "73" * 16384
                yield c
        for kind in range(13, 21):
            for slot in (0, 15):
                c = binding_case(f"signed_slot_bank_{bank}_kind_{kind}_slot_{slot}", kind, bank)
                c["buffers"]["entries"]["bytes"] = b"".join(
                    struct.pack("<I", kind | (slot << 8) | (2 << 16) | (3 << 24)) for _ in range(64)
                ).hex()
                yield c


    for bank in (0, 1):
        case = binding_case(f"zero_byte_scalar_upload_bank_{bank}", 26, bank)
        case["buffers"]["entries"]["bytes"] = b"".join(
            struct.pack("<I", 26 | (16 << 8) | (2 << 16)) for _ in range(64)
        ).hex()
        # Keep a non-null allocation and the observing memcpy stub even at zero bytes.
        yield case


    # Shift scalar destinations one DWORD into an existing record. Each store then
    # changes the source of the next load; preloading the whole record differs.
    for bank, index in ((0, 254), (1, 174)):
        case = binding_case(f"overlapping_scalar_copy_bank_{bank}", 16, bank)
        case["buffers"]["entries"]["bytes"] = b"".join(
            struct.pack("<I", 16 | (17 << 8) | (index << 16)) for _ in range(64)
        ).hex()
        yield case

    # Command storage may be the state object. A scripted descriptor writer changes
    # one mask word through that command argument; all other contributors stay fixed.
    for bank in (0, 1):
        base = 0x1900 if bank else 0
        for label, offset, value in (
            ("resource_low", 0x60, 1),
            ("resource_high", 0x68, 1),
            ("pending_low", 0x18, 14),
            ("pending_high", 0x20, 14),
            ("other_resource", 0x70, 1),
            ("other_pending", 0x28, 14),
        ):
            case = binding_case(f"final_masks_live_bank_{bank}_{label}", 0, bank)
            case["buffers"]["self"] = {"size": 0xB500, "bytes": "00" * 0xB500}
            case["args"]["rsi"] = "buf:self"
            case["memory"].extend([
                {"addr": "buf:self+8", "pointer": "self+0xb500"},
                {"addr": "buf:self+16", "pointer": f"self+{base + offset}"},
            ])
            case["stubs"][-1]["writes"] = [
                {"arg": 0, "offset": base + offset, "bytes": qword(value), "size": 8}
            ]
            yield case


def main():
    """Write deterministic synthetic cases to the requested directory."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        (out / (entry["id"] + ".json")).write_text(json.dumps(entry, indent=2) + "\n")
    print(f"{len(generated)} synthetic edge cases written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
