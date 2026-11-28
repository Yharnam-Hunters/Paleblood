#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD cleanup-adapter cases. Usage: SCRIPT OUT_DIR.

Only the original cleanup entry is scripted. Pointer tokens are observed without
dereferencing them; they do not establish valid inputs for the actual cleanup.
The two scripted results are the DWORD values found on its normal return paths.
"""
import json
from pathlib import Path
import sys


ADDRESS = "0x011189e0"
CLEANUP = "0x01118e90"
INTERFACE_OFFSET = 0x38
OWNER_BYTES = 0x100


def case(cid, pointer, result, *, incoming=None, writes=(), stack=()):
    """Observe the complete owner, one cleanup argument and its DWORD result."""
    assert result in (0, 0x21)
    args = {"rdi": pointer, "rsi": "0x0123456789abcdef",
            "rdx": "0xfedcba9876543210", "rcx": "0x8000000000000001",
            "r8": "0xffffffffffffffff", "r9": "0x100000000",
            "rax": "0xaaaaaaaa55555555"}
    assert not set(incoming or {}) & {"rdi"}
    args.update(incoming or {})
    observer = {"address": CLEANUP, "argc": 1, "argi": [0],
                "ret": hex(result), "writes": list(writes)}
    extra = {"address": CLEANUP, "argc": 1, "argi": [0], "ret": "0x21"}
    row = {"schema": 1, "address": ADDRESS, "id": cid, "returns": "i32",
           "args": args,
           "buffers": {"owner": {"size": OWNER_BYTES,
                         "bytes": bytes((i * 37 + 0xa5) & 255
                                        for i in range(OWNER_BYTES)).hex()}},
           "memory": [], "imports": [], "stubs": [observer, extra]}
    if stack:
        row["stack"] = list(stack)
    return row


def cases():
    """Separate null forwarding, full-width adjustment, effects and result paths."""
    rows = []
    tokens = (0, 1, 7, 0x37, 0x38, 0x39, 0x7fffffff, 0x80000000,
              0xffffffff, 0x100000000, 0x100000038, 0x7fffffffffffffff,
              0x8000000000000000, 0x8000000000000038, 0xffffffff00000000,
              0xfffffffffffffff0, 0xffffffffffffffff)
    for pointer in tokens:
        for result in (0, 0x21):
            rows.append(case(f"token-{pointer:016x}-{result:02x}", hex(pointer), result))
    for offset in (0, 1, 7, 0x20, 0x80, OWNER_BYTES - INTERFACE_OFFSET - 1):
        for result in (0, 0x21):
            for effects in (False, True):
                writes = [{"arg": 0, "offset": 0, "bytes": "6d"}] if effects else []
                rows.append(case(f"owned-{offset:02x}-{result:02x}-{int(effects)}",
                                 f"buf:owner+{offset + INTERFACE_OFFSET}", result,
                                 writes=writes))
    for register in ("rsi", "rdx", "rcx", "r8", "r9", "rax"):
        boundary = "0x8000000000000000" if register == "r8" else "0xffffffffffffffff"
        for value in ("0x0", boundary, "buf:owner+7"):
            rows.append(case(f"unused-{register}-{value.replace(':', '-').replace('+', '-')}",
                             "buf:owner+56", 0x21, incoming={register: value}))
    for index, values in enumerate((("0x0",) * 4, ("0xffffffffffffffff",) * 4,
                                    ("buf:owner", "0x1", "0x100000000", "0x8000000000000000"))):
        rows.append(case(f"unused-stack-{index}", "buf:owner+56", 0x21, stack=values))
    return rows


def main():
    """Write deterministic synthetic fixtures and reject duplicate identities."""
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    rows = cases()
    assert len({row["id"] for row in rows}) == len(rows)
    inputs = [{key: value for key, value in row.items() if key != "id"} for row in rows]
    assert len({json.dumps(row, sort_keys=True) for row in inputs}) == len(rows)
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    for row in rows:
        (directory / (row["id"] + ".json")).write_text(json.dumps(row, indent=2) + "\n")
    print(f"wrote {len(rows)} synthetic cleanup-adapter cases")


if __name__ == "__main__":
    main()
