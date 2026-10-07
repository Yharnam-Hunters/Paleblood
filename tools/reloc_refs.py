#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pointers to functions that the loader writes (PS4 relocations), which Ghidra may not show.

usage: reloc_refs.py ELF ADDRESS [ADDRESS ...] [--base 0x400000]

Vtables and function-pointer tables in a PS4 executable are zero in the file and filled at load
time by R_X86_64_RELATIVE relocations in the dynlib data segment (DT_SCE_RELA). A function with
no direct caller and no relocation pointing at it is never reached in the stock game. Prints,
per address, every slot (in ADDRESS's convention: ELF vaddr + --base) whose relocation points at
it. Reads the ELF only.
"""
import argparse
import bisect
import struct
import sys

PT_DYNAMIC, PT_SCE_DYNLIBDATA = 2, 0x61000000
DT_NULL, DT_SCE_RELA, DT_SCE_RELASZ = 0, 0x6100002f, 0x61000031
R_X86_64_RELATIVE = 8


def relative_pointers(path: str, base: int) -> list:
    """Sorted (target, slot) pairs for every RELATIVE relocation, both shifted by base."""
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'\x7fELF':
        raise SystemExit(f'{path}: not an ELF file')
    phoff, = struct.unpack_from('<Q', d, 0x20)
    phnum, = struct.unpack_from('<H', d, 0x38)
    dynamic = dynlib = None
    for i in range(phnum):
        p_type, _, p_off, _, _, p_filesz = struct.unpack_from('<IIQQQQ', d, phoff + 56 * i)
        if p_type == PT_DYNAMIC:
            dynamic = (p_off, p_filesz)
        elif p_type == PT_SCE_DYNLIBDATA:
            dynlib = p_off
    if dynamic is None or dynlib is None:
        raise SystemExit(f'{path}: no PT_DYNAMIC or PT_SCE_DYNLIBDATA segment (not a PS4 executable?)')
    tags = {}
    for off in range(dynamic[0], dynamic[0] + dynamic[1], 16):
        tag, val = struct.unpack_from('<qQ', d, off)
        if tag == DT_NULL:
            break
        tags.setdefault(tag, val)
    if DT_SCE_RELA not in tags or DT_SCE_RELASZ not in tags:
        raise SystemExit(f'{path}: no DT_SCE_RELA table')
    out = []
    start = dynlib + tags[DT_SCE_RELA]
    for off in range(start, start + tags[DT_SCE_RELASZ], 24):
        r_offset, r_info, r_addend = struct.unpack_from('<QQq', d, off)
        if r_info & 0xffffffff == R_X86_64_RELATIVE:
            out.append((r_addend + base, r_offset + base))
    out.sort()
    return out


def slots_for(pointers: list, address: int) -> list:
    i = bisect.bisect_left(pointers, (address, -1))
    found = []
    while i < len(pointers) and pointers[i][0] == address:
        found.append(pointers[i][1])
        i += 1
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('elf')
    ap.add_argument('addresses', nargs='+')
    ap.add_argument('--base', default='0x400000')
    a = ap.parse_args()
    pointers = relative_pointers(a.elf, int(a.base, 16))
    for text in a.addresses:
        addr = int(text, 16)
        slots = slots_for(pointers, addr)
        shown = ' '.join(f'0x{s:08x}' for s in slots[:8]) + (' ...' if len(slots) > 8 else '')
        print(f'0x{addr:08x} {len(slots)} pointer(s) {shown}'.rstrip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
