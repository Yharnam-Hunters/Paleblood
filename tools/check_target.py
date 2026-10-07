#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check converted executables against the pinned target.

  check_target.py ELF_DIR

- every file named in target.sha256 must exist in ELF_DIR with that SHA-256;
- the loaded image of the main executable (its loadable segments copied to their p_vaddr)
  must match target.image.sha256. That hash does not depend on the SELF to ELF tool.
Prints one line per file and exits 1 on any mismatch. Reads only.
"""
from __future__ import annotations

import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pins(name: str) -> list[tuple[str, str]]:
    with open(os.path.join(ROOT, name)) as f:
        return [tuple(ln.split(None, 1)) for ln in f if ln.strip()]


def loaded_image_sha256(path: str) -> str:
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'\x7fELF':
        raise ValueError(f'{path}: not an ELF')
    phoff, = struct.unpack_from('<Q', d, 32)
    n, = struct.unpack_from('<H', d, 56)
    ph = [struct.unpack_from('<IIQQQQQQ', d, phoff + 56 * i) for i in range(n)]
    loads = [p for p in ph if p[0] in (1, 0x61000010)]
    img = bytearray(max(p[3] + p[6] for p in loads))
    for _t, _f, off, va, _pa, fs, _ms, _al in loads:
        img[va:va + fs] = d[off:off + fs]
    return hashlib.sha256(img).hexdigest()


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    elf_dir = sys.argv[1]
    bad = 0
    for want, name in pins('target.sha256'):
        name = name.strip().lstrip('*')
        path = os.path.join(elf_dir, name)
        if not os.path.isfile(path):
            print(f'MISSING  {name}')
            bad += 1
            continue
        with open(path, 'rb') as f:
            got = hashlib.sha256(f.read()).hexdigest()
        ok = got == want
        bad += not ok
        print(f"{'ok      ' if ok else 'MISMATCH'} {name}")
    for want, name in pins('target.image.sha256'):
        name = name.strip()
        path = os.path.join(elf_dir, name)
        try:
            got = loaded_image_sha256(path)
        except (OSError, ValueError) as e:
            print(f'MISSING  {name} (loaded image): {e}')
            bad += 1
            continue
        ok = got == want
        bad += not ok
        print(f"{'ok      ' if ok else 'MISMATCH'} {name} (loaded image)")
    if bad:
        print(f'{bad} problem(s): this is not the target build (see README, Target)', file=sys.stderr)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
